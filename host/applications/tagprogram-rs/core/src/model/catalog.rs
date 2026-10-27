//! Published firmware releases and the local cache of their images.
//!
//! Port of qtprogram's `firmwarecatalog.cpp`. A release is a GitHub release
//! tagged `fw-vN(.N)*` whose `tag-firmware-<tag>.tar.gz` asset holds
//! `firmware/<Target>/<Target>.elf` and `<Target>-build-manifest.json` for
//! each target. A release is cached once its archive has been downloaded,
//! the firmware members extracted, every image verified against its
//! manifest, and the directory installed as `<cache>/<tag>` with a
//! `.verified` marker. The last listing is kept in `<cache>/releases.json`
//! so the catalog works offline.
//!
//! The network calls are blocking; the engine runs them on a worker thread.
//! A [`Catalog`] is `Sync`, so it can be shared with that thread.
//!
//! The cache directory is chosen by the caller; the app uses `firmware/` in
//! the platform data directory. Its layout:
//!
//! ```text
//! <cache>/releases.json                     last listing (see listing_to_json)
//! <cache>/<tag>/.verified                   present only once every image verified
//! <cache>/<tag>/firmware/<T>/<T>.elf
//! <cache>/<tag>/firmware/<T>/<T>-build-manifest.json
//! <cache>/<tag>.partial/                    staging while a fetch runs
//! ```
//!
//! Only archive members matching the last two shapes are ever written, which
//! is what keeps a hostile archive from writing outside the cache. A release
//! is never trusted because it was downloaded: it must verify first.

use chrono::{DateTime, SecondsFormat, Utc};
use regex::Regex;
use serde::Serialize;
use std::path::{Path, PathBuf};
use std::sync::{LazyLock, Mutex};
use std::time::Duration;

use super::image::{FirmwareImage, load_firmware_image};

/// The GitHub API listing of the repository's releases.
pub const RELEASES_URL: &str =
    "https://api.github.com/repos/tag-designs/software/releases?per_page=100";
/// `User-Agent` sent with every request; the GitHub API refuses requests
/// without one.
const USER_AGENT: &str = "tagprogram";
/// Empty file in `<cache>/<tag>` whose presence means the release's images
/// were all verified when installed.
const VERIFIED_MARKER: &str = ".verified";
/// File in the cache directory holding the last listing.
const LISTING_FILE: &str = "releases.json";
/// Most pages of the release listing [`Catalog::refresh`] follows, 100
/// releases each: a bound on a server whose `Link` header never ends.
pub const MAX_LISTING_PAGES: usize = 20;

/// The `rel="next"` URL of a GitHub `Link` header, if there is one.
///
/// ```
/// use tagprogram_core::model::catalog::next_page;
/// let link = r#"<https://api.github.com/x?page=2>; rel="next", <https://api.github.com/x?page=5>; rel="last""#;
/// assert_eq!(next_page(link).as_deref(), Some("https://api.github.com/x?page=2"));
/// assert_eq!(next_page(r#"<https://api.github.com/x?page=1>; rel="prev""#), None);
/// ```
pub fn next_page(link: &str) -> Option<String> {
    link.split(',').find_map(|part| {
        let (url, params) = part.split_once(';')?;
        params
            .split(';')
            .any(|p| p.trim() == r#"rel="next""#)
            .then(|| {
                url.trim()
                    .trim_start_matches('<')
                    .trim_end_matches('>')
                    .to_owned()
            })
    })
}

/// A firmware release tag: `fw-v` and dot-separated decimal numbers.
static FIRMWARE_TAG: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^fw-v\d+(\.\d+)*$").unwrap());
/// A qualification asset; capture 1 is the target, the date suffix is
/// optional and must be eight digits when present.
static QUALIFICATION: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^qualification-([A-Za-z0-9_]+)(-\d{8})?\.tar\.gz$").unwrap());
// The regex crate has no backreferences, so the `\1` of qtprogram's pattern
// is checked by hand in `acceptable_member`.
/// An archive member the cache keeps, before the directory and file stems
/// are compared; captures 1 and 2 are the two target names.
static MEMBER: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^firmware/([A-Za-z0-9_-]+)/([A-Za-z0-9_-]+)(\.elf|-build-manifest\.json)$")
        .unwrap()
});

/// One published (or cached) firmware release.
///
/// Port of qtprogram's `FirmwareRelease`. Serialized into the view state.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
pub struct FirmwareRelease {
    /// e.g. `"fw-v0.7"`.
    pub tag: String,
    /// When it was published; `None` for a cached release the listing lacks.
    pub published: Option<DateTime<Utc>>,
    /// Marked as a prerelease on GitHub.
    pub prerelease: bool,
    /// The `tag-firmware-<tag>.tar.gz` asset's download URL, or empty.
    pub archive_url: String,
    /// The release page, whose notes say what was qualified, or empty.
    pub page_url: String,
    /// Targets qualified on this release: those with a
    /// `qualification-<Target>-<date>.tar.gz` asset. Meaningful only when
    /// `listed`.
    pub qualified: Vec<String>,
    /// Its assets were read from a listing, so `qualified` is known.
    pub listed: bool,
    /// Its images are in the cache and verified.
    pub cached: bool,
}

impl FirmwareRelease {
    /// `target` is known to be qualified on this release.
    pub fn is_qualified(&self, target: &str) -> bool {
        self.listed && self.qualified.iter().any(|t| t == target)
    }
}

/// `tag` names a firmware release (`fw-v0.7`, `fw-v1.2.3`).
///
/// Also what tells a release directory in the cache from anything else there
/// (such as a `<tag>.partial` staging directory).
///
/// # Examples
///
/// ```
/// use tagprogram_core::model::catalog::is_firmware_tag;
///
/// assert!(is_firmware_tag("fw-v1.2.3"));
/// assert!(!is_firmware_tag("fw-v0.6.partial"));
/// assert!(!is_firmware_tag("v2.0"));
/// ```
pub fn is_firmware_tag(tag: &str) -> bool {
    FIRMWARE_TAG.is_match(tag)
}

/// The target a `qualification-<Target>[-<yyyymmdd>].tar.gz` asset
/// qualifies, or `None` for any other asset.
pub fn qualification_target(asset_name: &str) -> Option<&str> {
    QUALIFICATION
        .captures(asset_name)
        .map(|c| c.get(1).unwrap().as_str())
}

/// An archive member the cache keeps: `firmware/<T>/<T>.elf` or
/// `firmware/<T>/<T>-build-manifest.json`.
///
/// `<T>` is letters, digits, `_` and `-`, and the directory and file stems
/// must be equal. `name` must already have any leading `./` removed (the
/// extractor does that); with it, the name is refused. No `..` or absolute
/// path can match, so an accepted member always lands inside the cache.
pub fn acceptable_member(name: &str) -> bool {
    MEMBER.captures(name).is_some_and(|c| c[1] == c[2])
}

/// The numeric version of a release tag, for ordering: `fw-v0.10` gives
/// `[0, 10]`. Parsing stops at the first component that is not a number.
fn version_of(tag: &str) -> Vec<u64> {
    tag.get(4..)
        .unwrap_or("")
        .split('.')
        .map_while(|n| n.parse().ok())
        .collect()
}

/// Sorts `releases` newest version first, numerically (`fw-v0.10` before
/// `fw-v0.9`).
fn sort_by_version(releases: &mut [FirmwareRelease]) {
    releases.sort_by_key(|r| std::cmp::Reverse(version_of(&r.tag)));
}

/// Parses an RFC 3339 timestamp into UTC; `None` when empty or malformed.
fn parse_time(text: &str) -> Option<DateTime<Utc>> {
    DateTime::parse_from_rfc3339(text)
        .ok()
        .map(|t| t.with_timezone(&Utc))
}

/// Parses a GitHub releases listing: firmware releases with an archive,
/// drafts skipped, newest version first.
///
/// Input is the JSON array the GitHub REST API returns from
/// [`RELEASES_URL`]. Releases whose tag is not a firmware tag, and those
/// without a `tag-firmware-<tag>.tar.gz` asset, are left out. Every result is
/// `listed`, with its qualified targets in asset order, without duplicates.
/// Malformed JSON gives an empty list.
pub fn parse_releases(json: &str) -> Vec<FirmwareRelease> {
    let value: serde_json::Value = serde_json::from_str(json).unwrap_or_default();
    let mut releases = Vec::new();
    for o in value.as_array().into_iter().flatten() {
        let tag = o["tag_name"].as_str().unwrap_or("");
        if o["draft"].as_bool().unwrap_or(false) || !is_firmware_tag(tag) {
            continue;
        }
        let archive = format!("tag-firmware-{tag}.tar.gz");
        let mut r = FirmwareRelease {
            tag: tag.into(),
            published: o["published_at"].as_str().and_then(parse_time),
            prerelease: o["prerelease"].as_bool().unwrap_or(false),
            page_url: o["html_url"].as_str().unwrap_or("").into(),
            listed: true,
            ..Default::default()
        };
        for a in o["assets"].as_array().into_iter().flatten() {
            let name = a["name"].as_str().unwrap_or("");
            if name == archive {
                r.archive_url = a["browser_download_url"].as_str().unwrap_or("").into();
            }
            if let Some(t) = qualification_target(name)
                && !r.qualified.iter().any(|q| q == t)
            {
                r.qualified.push(t.into());
            }
        }
        if !r.archive_url.is_empty() {
            releases.push(r);
        }
    }
    sort_by_version(&mut releases);
    releases
}

/// Serialises releases as `releases.json` (qtprogram's format: `tag`,
/// `published` ISO 8601, `prerelease`, `archive`, `page`, `qualified`).
///
/// A pretty-printed JSON array. `published` is RFC 3339 UTC to the second
/// with a `Z` suffix, or `""` when unknown. `listed` and `cached` are not
/// written: the first is implied by the `qualified` key and the second is
/// recomputed from the cache.
pub fn listing_to_json(releases: &[FirmwareRelease]) -> String {
    let array: Vec<serde_json::Value> = releases
        .iter()
        .map(|r| {
            serde_json::json!({
                "tag": r.tag,
                "published": r.published
                    .map(|t| t.to_rfc3339_opts(SecondsFormat::Secs, true))
                    .unwrap_or_default(),
                "prerelease": r.prerelease,
                "archive": r.archive_url,
                "page": r.page_url,
                "qualified": r.qualified,
            })
        })
        .collect();
    serde_json::to_string_pretty(&array).unwrap()
}

/// Reads `releases.json`. A release written before qualification was
/// recorded (no `qualified` key) is not `listed`.
///
/// Entries whose `tag` is not a firmware tag are dropped; malformed JSON
/// gives an empty list. `cached` is always false here; [`Catalog`] sets it.
pub fn listing_from_json(json: &str) -> Vec<FirmwareRelease> {
    let value: serde_json::Value = serde_json::from_str(json).unwrap_or_default();
    value
        .as_array()
        .into_iter()
        .flatten()
        .map(|o| FirmwareRelease {
            tag: o["tag"].as_str().unwrap_or("").into(),
            published: o["published"].as_str().and_then(parse_time),
            prerelease: o["prerelease"].as_bool().unwrap_or(false),
            archive_url: o["archive"].as_str().unwrap_or("").into(),
            page_url: o["page"].as_str().unwrap_or("").into(),
            listed: o.get("qualified").is_some(),
            qualified: o["qualified"]
                .as_array()
                .into_iter()
                .flatten()
                .filter_map(|t| t.as_str().map(str::to_string))
                .collect(),
            cached: false,
        })
        .filter(|r| is_firmware_tag(&r.tag))
        .collect()
}

/// The releases offered, over a cache directory.
///
/// Port of qtprogram's `FirmwareCatalog`, without its signals: the engine
/// calls [`Catalog::refresh`] and [`Catalog::fetch`] on a worker thread and
/// rereads [`Catalog::releases`] when they return. Interior mutexes make
/// every method take `&self`; a panic while one is held poisons it, and
/// later calls then panic too.
pub struct Catalog {
    /// Root of the cache; see the module documentation for its layout.
    cache_dir: PathBuf,
    /// The releases offered, newest first: the last listing plus any cached
    /// release it lacks.
    releases: Mutex<Vec<FirmwareRelease>>,
    /// The tag of the release being fetched, so at most one download runs.
    fetching: Mutex<Option<String>>,
}

impl Catalog {
    /// A catalog over `cache_dir` (created if missing), loaded from its
    /// `releases.json`.
    ///
    /// A directory that cannot be created is not an error here: the catalog
    /// is then empty, and a later fetch reports the failure to write.
    pub fn new(cache_dir: impl Into<PathBuf>) -> Catalog {
        let catalog = Catalog {
            cache_dir: cache_dir.into(),
            releases: Mutex::new(Vec::new()),
            fetching: Mutex::new(None),
        };
        let _ = std::fs::create_dir_all(&catalog.cache_dir);
        catalog.load();
        catalog
    }

    /// The cache directory.
    pub fn cache_dir(&self) -> &Path {
        &self.cache_dir
    }

    /// Reloads the offered releases from `releases.json` and the cache.
    ///
    /// A missing or unreadable listing counts as empty, so only cached
    /// releases are offered.
    pub fn load(&self) {
        let listed = std::fs::read_to_string(self.cache_dir.join(LISTING_FILE))
            .map(|j| listing_from_json(&j))
            .unwrap_or_default();
        self.set_releases(listed);
    }

    /// Offers `listed` plus every cached release it lacks (offline, or
    /// withdrawn since: its images were verified when extracted), newest
    /// first.
    fn set_releases(&self, mut listed: Vec<FirmwareRelease>) {
        if let Ok(dir) = std::fs::read_dir(&self.cache_dir) {
            for entry in dir.flatten() {
                let tag = entry.file_name().to_string_lossy().into_owned();
                if entry.path().is_dir()
                    && self.is_cached(&tag)
                    && !listed.iter().any(|r| r.tag == tag)
                {
                    listed.push(FirmwareRelease {
                        tag,
                        ..Default::default()
                    });
                }
            }
        }
        for r in &mut listed {
            r.cached = self.is_cached(&r.tag);
        }
        sort_by_version(&mut listed);
        *self.releases.lock().unwrap() = listed;
    }

    /// The releases offered, newest first.
    pub fn releases(&self) -> Vec<FirmwareRelease> {
        self.releases.lock().unwrap().clone()
    }

    /// The release tagged `tag`, if offered.
    pub fn release(&self, tag: &str) -> Option<FirmwareRelease> {
        self.releases
            .lock()
            .unwrap()
            .iter()
            .find(|r| r.tag == tag)
            .cloned()
    }

    /// `tag`'s images are in the cache and were verified.
    ///
    /// Decided by the `.verified` marker alone; the images are not hashed
    /// again here (loading them through [`Catalog::images`] does that).
    pub fn is_cached(&self, tag: &str) -> bool {
        is_firmware_tag(tag) && self.cache_dir.join(tag).join(VERIFIED_MARKER).exists()
    }

    /// The release being fetched, if any.
    pub fn fetching(&self) -> Option<String> {
        self.fetching.lock().unwrap().clone()
    }

    /// A blocking HTTP client with this program's user agent, a 20 s
    /// connect timeout and `timeout` for the whole request.
    ///
    /// # Errors
    ///
    /// The client builder's error, as text (e.g. no TLS backend).
    fn client(timeout: Duration) -> Result<reqwest::blocking::Client, String> {
        reqwest::blocking::Client::builder()
            .user_agent(USER_AGENT)
            .connect_timeout(Duration::from_secs(20))
            .timeout(timeout)
            .build()
            .map_err(|e| e.to_string())
    }

    /// Lists the releases on GitHub (blocking), saves the listing as
    /// `releases.json` and offers it.
    ///
    /// On failure the offered releases are unchanged. A 403 or 429 is
    /// reported as the rate limit; anything else as the transport's error.
    ///
    /// Each request has a 30 s overall timeout. The listing comes 100
    /// releases a page; further pages are followed through the `Link`
    /// header's `rel="next"`, up to [`MAX_LISTING_PAGES`]. A failure to write
    /// `releases.json` is logged as a warning: the listing is still offered,
    /// just not kept for offline use.
    ///
    /// # Errors
    ///
    /// The rate-limit message, an HTTP status error, or a transport error,
    /// each as display-ready text.
    pub fn refresh(&self) -> Result<(), String> {
        let client = Self::client(Duration::from_secs(30))?;
        let mut listed = Vec::new();
        let mut url = Some(RELEASES_URL.to_owned());
        for _ in 0..MAX_LISTING_PAGES {
            let Some(page) = url.take() else { break };
            let reply = client
                .get(&page)
                .header("Accept", "application/vnd.github+json")
                .send()
                .map_err(|e| e.to_string())?;
            let status = reply.status().as_u16();
            if status == 403 || status == 429 {
                return Err("GitHub refused the listing (rate limit); try again later.".into());
            }
            let reply = reply.error_for_status().map_err(|e| e.to_string())?;
            url = reply
                .headers()
                .get("link")
                .and_then(|v| v.to_str().ok())
                .and_then(next_page);
            listed.extend(parse_releases(&reply.text().map_err(|e| e.to_string())?));
        }
        if let Err(e) = std::fs::write(self.cache_dir.join(LISTING_FILE), listing_to_json(&listed))
        {
            log::warn!("Could not save the release listing for offline use: {e}");
        }
        self.set_releases(listed);
        Ok(())
    }

    /// Downloads, extracts, verifies and installs release `tag` (blocking).
    ///
    /// Already cached: nothing to do. Refuses while another release is
    /// downloading, or when the release has no archive. Any failure removes
    /// the staging directory, so a release is either fully cached and
    /// verified or absent.
    ///
    /// The archive is downloaded into `<cache>/<tag>.partial` (600 s overall
    /// timeout), extracted there, then installed by `Catalog::install`.
    /// `tag` must be offered with an archive URL, so call
    /// [`Catalog::refresh`] first for a release not yet listed. The offered
    /// releases are recomputed afterwards, success or not.
    ///
    /// # Errors
    ///
    /// "Another release is downloading.", `"<tag> has no firmware archive."`,
    /// or the download, extraction, verification or install failure.
    pub fn fetch(&self, tag: &str) -> Result<(), String> {
        if self.is_cached(tag) {
            return Ok(());
        }
        let archive_url = self.release(tag).map(|r| r.archive_url).unwrap_or_default();
        {
            let mut fetching = self.fetching.lock().unwrap();
            if fetching.is_some() {
                return Err("Another release is downloading.".into());
            }
            if archive_url.is_empty() {
                return Err(format!("{tag} has no firmware archive."));
            }
            *fetching = Some(tag.into());
        }
        let staging = self.cache_dir.join(format!("{tag}.partial"));
        let result = self.download_and_install(tag, &archive_url, &staging);
        *self.fetching.lock().unwrap() = None;
        if let Err(why) = &result {
            log::warn!("Fetching {tag} failed: {why}");
        }
        self.set_releases(self.releases());
        result
    }

    /// Downloads `url` into `staging/archive.tar.gz`, after clearing
    /// `staging`, and installs it as `tag`.
    ///
    /// # Errors
    ///
    /// `"Cannot write <path>."` when staging cannot be created, a transport or
    /// HTTP status error, or [`Catalog::install`]'s error. `staging` is
    /// removed on every failure.
    fn download_and_install(&self, tag: &str, url: &str, staging: &Path) -> Result<(), String> {
        let _ = std::fs::remove_dir_all(staging);
        std::fs::create_dir_all(staging)
            .map_err(|_| format!("Cannot write {}.", staging.display()))?;
        let archive = staging.join("archive.tar.gz");
        let download = || -> Result<(), String> {
            let mut reply = Self::client(Duration::from_secs(600))?
                .get(url)
                .send()
                .and_then(|r| r.error_for_status())
                .map_err(|e| e.to_string())?;
            let mut file = std::fs::File::create(&archive)
                .map_err(|_| format!("Cannot write {}.", archive.display()))?;
            reply.copy_to(&mut file).map_err(|e| e.to_string())?;
            Ok(())
        };
        if let Err(why) = download() {
            let _ = std::fs::remove_dir_all(staging);
            return Err(why);
        }
        self.install(tag, &archive, staging)
    }

    /// Extracts the firmware members of `archive` into `staging`, verifies
    /// every image and installs `staging` as `<cache>/<tag>`. On failure the
    /// staging (or half-installed) directory is removed.
    ///
    /// The archive itself is deleted once extracted. Every
    /// `firmware/<T>/` directory must hold a `<T>.elf` that
    /// [`load_firmware_image`] accepts as a release image (manifest present
    /// and matching); one failure refuses the whole release. An existing
    /// `<cache>/<tag>` is replaced. The `.verified` marker is written last,
    /// so an interrupted install never looks cached.
    ///
    /// # Errors
    ///
    /// [`extract_firmware`]'s error, the first image's refusal, or
    /// `"Cannot install the release in <dir>."`
    fn install(&self, tag: &str, archive: &Path, staging: &Path) -> Result<(), String> {
        let fail = |dir: &Path, why: String| {
            let _ = std::fs::remove_dir_all(dir);
            Err(why)
        };
        if let Err(why) = extract_firmware(archive, staging) {
            return fail(staging, why);
        }
        let _ = std::fs::remove_file(archive);
        // Every image must verify before the release counts as cached; one
        // bad image refuses the whole release.
        let firmware = staging.join("firmware");
        for target in subdirectories(&firmware) {
            let image = load_firmware_image(
                &firmware.join(&target).join(format!("{target}.elf")),
                Some(tag),
            );
            if !image.usable() {
                return fail(staging, image.error);
            }
        }
        let fin = self.cache_dir.join(tag);
        let _ = std::fs::remove_dir_all(&fin);
        let cannot = format!("Cannot install the release in {}.", fin.display());
        if std::fs::rename(staging, &fin).is_err() {
            return fail(staging, cannot);
        }
        if std::fs::write(fin.join(VERIFIED_MARKER), b"").is_err() {
            return fail(&fin, cannot);
        }
        Ok(())
    }

    /// The targets cached for `tag`, sorted; empty unless it is cached.
    ///
    /// A target is a `firmware/<T>/` directory that holds `<T>.elf`.
    pub fn targets(&self, tag: &str) -> Vec<String> {
        if !self.is_cached(tag) {
            return Vec::new();
        }
        subdirectories(&self.cache_dir.join(tag).join("firmware"))
            .into_iter()
            .filter(|t| self.image_path(tag, t).exists())
            .collect()
    }

    /// Where `target`'s image for `tag` is (or would be) cached.
    pub fn image_path(&self, tag: &str, target: &str) -> PathBuf {
        self.cache_dir
            .join(tag)
            .join("firmware")
            .join(target)
            .join(format!("{target}.elf"))
    }

    /// Every cached image of `tag`, loaded and verified.
    ///
    /// Each image is hashed and checked against its manifest again, so a
    /// cached file altered since install comes back with an `error`.
    pub fn images(&self, tag: &str) -> Vec<FirmwareImage> {
        self.targets(tag)
            .iter()
            .map(|t| load_firmware_image(&self.image_path(tag, t), Some(tag)))
            .collect()
    }
}

/// Names of the subdirectories of `dir`, sorted; empty when `dir` cannot be
/// read.
fn subdirectories(dir: &Path) -> Vec<String> {
    let mut out: Vec<String> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.path().is_dir())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .collect();
    out.sort();
    out
}

/// Extracts the acceptable members of the gzipped tar `archive` under
/// `dest`; every other member is ignored, so nothing can be written outside
/// `dest`.
///
/// Members are matched after removing one leading `./`; only regular files
/// are extracted (links and directories are skipped). An existing file is
/// overwritten.
///
/// # Errors
///
/// `"The archive could not be read: <error>"` (not gzip, not tar, or
/// truncated), `"Extraction failed: <error>"` when a file cannot be written,
/// or "The archive holds no firmware images." when no member was kept.
fn extract_firmware(archive: &Path, dest: &Path) -> Result<(), String> {
    let unreadable = |e: std::io::Error| format!("The archive could not be read: {e}");
    let file = std::fs::File::open(archive).map_err(unreadable)?;
    let mut tar = tar::Archive::new(flate2::read::GzDecoder::new(file));
    let mut kept = 0;
    for entry in tar.entries().map_err(unreadable)? {
        let mut entry = entry.map_err(unreadable)?;
        let path = entry
            .path()
            .map_err(unreadable)?
            .to_string_lossy()
            .into_owned();
        let name = path.strip_prefix("./").unwrap_or(&path);
        if !entry.header().entry_type().is_file() || !acceptable_member(name) {
            continue;
        }
        let out = dest.join(name);
        let mut extract = || -> std::io::Result<()> {
            std::fs::create_dir_all(out.parent().unwrap())?;
            let mut file = std::fs::File::create(&out)?;
            std::io::copy(&mut entry, &mut file)?;
            Ok(())
        };
        extract().map_err(|e| format!("Extraction failed: {e}"))?;
        kept += 1;
    }
    if kept == 0 {
        return Err("The archive holds no firmware images.".into());
    }
    Ok(())
}

/// Unit tests of the patterns, listings, cache discovery and install, with
/// no network access.
#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::elf::testelf;
    use crate::model::image::sha256_of_file;

    /// A GitHub listing covering a qualified release, a prerelease, a draft,
    /// a release without an archive and a non-firmware tag.
    const GITHUB: &str = r#"[
      {"tag_name": "fw-v0.6", "draft": false, "prerelease": false,
       "published_at": "2026-10-01T12:00:00Z",
       "html_url": "https://github.com/tag-designs/software/releases/tag/fw-v0.6",
       "assets": [
         {"name": "tag-firmware-fw-v0.6.tar.gz", "browser_download_url": "https://x/fw-v0.6.tgz"},
         {"name": "qualification-BitTag-20261002.tar.gz", "browser_download_url": "https://x/q"},
         {"name": "qualification-BitTag.tar.gz", "browser_download_url": "https://x/q2"},
         {"name": "qualification-PresTag.tar.gz", "browser_download_url": "https://x/q3"}]},
      {"tag_name": "fw-v0.10", "draft": false, "prerelease": true,
       "published_at": "2026-10-20T08:30:00Z", "html_url": "p10",
       "assets": [{"name": "tag-firmware-fw-v0.10.tar.gz", "browser_download_url": "a10"}]},
      {"tag_name": "fw-v0.7", "draft": true, "assets": [
         {"name": "tag-firmware-fw-v0.7.tar.gz", "browser_download_url": "a7"}]},
      {"tag_name": "fw-v0.8", "draft": false, "assets": []},
      {"tag_name": "v2.0", "draft": false, "assets": [
         {"name": "tag-firmware-v2.0.tar.gz", "browser_download_url": "a"}]}
    ]"#;

    #[test]
    fn patterns() {
        assert!(is_firmware_tag("fw-v0.6") && is_firmware_tag("fw-v1.2.3"));
        assert!(!is_firmware_tag("fw-v0.6.partial") && !is_firmware_tag("v2.0"));
        assert_eq!(
            qualification_target("qualification-BitTag-20261002.tar.gz"),
            Some("BitTag")
        );
        assert_eq!(
            qualification_target("qualification-BitTag-2026.tar.gz"),
            None
        );
        assert!(acceptable_member("firmware/BitTag/BitTag.elf"));
        assert!(acceptable_member(
            "firmware/BitTag/BitTag-build-manifest.json"
        ));
        assert!(!acceptable_member("firmware/BitTag/PresTag.elf"));
        assert!(!acceptable_member("firmware/../BitTag/BitTag.elf"));
        assert!(!acceptable_member("firmware/BitTag/BitTag.bin"));
    }

    #[test]
    fn parses_github_listing() {
        let r = parse_releases(GITHUB);
        let tags: Vec<&str> = r.iter().map(|r| r.tag.as_str()).collect();
        assert_eq!(tags, ["fw-v0.10", "fw-v0.6"]);
        assert!(r[0].prerelease);
        assert_eq!(r[1].archive_url, "https://x/fw-v0.6.tgz");
        assert_eq!(r[1].qualified, ["BitTag", "PresTag"]);
        assert!(r[1].is_qualified("BitTag") && !r[1].is_qualified("IMUTag"));
        assert_eq!(
            r[1].published.unwrap().to_rfc3339(),
            "2026-10-01T12:00:00+00:00"
        );
    }

    #[test]
    fn listing_round_trip_and_old_listings() {
        let r = parse_releases(GITHUB);
        let json = listing_to_json(&r);
        assert!(json.contains("\"published\": \"2026-10-01T12:00:00Z\""));
        assert_eq!(listing_from_json(&json), r);
        let old = r#"[{"tag": "fw-v0.5", "published": "", "archive": "a"}, {"tag": "x"}]"#;
        let r = listing_from_json(old);
        assert_eq!(r.len(), 1);
        assert!(!r[0].listed && r[0].published.is_none());
    }

    #[test]
    fn cached_releases_are_offered_without_a_listing() {
        let dir = tempfile::tempdir().unwrap();
        let fw = dir.path().join("fw-v0.5/firmware/BitTag");
        std::fs::create_dir_all(&fw).unwrap();
        std::fs::write(fw.join("BitTag.elf"), b"").unwrap();
        std::fs::create_dir_all(dir.path().join("fw-v0.4/firmware/X")).unwrap(); // not verified
        std::fs::write(dir.path().join("fw-v0.5/.verified"), b"").unwrap();
        std::fs::write(
            dir.path().join("releases.json"),
            listing_to_json(&parse_releases(GITHUB)),
        )
        .unwrap();
        let c = Catalog::new(dir.path());
        let tags: Vec<String> = c.releases().iter().map(|r| r.tag.clone()).collect();
        assert_eq!(tags, ["fw-v0.10", "fw-v0.6", "fw-v0.5"]);
        assert!(c.release("fw-v0.5").unwrap().cached);
        assert!(!c.release("fw-v0.6").unwrap().cached);
        assert_eq!(c.targets("fw-v0.5"), ["BitTag"]);
        assert!(c.targets("fw-v0.4").is_empty());
        assert_eq!(c.fetch("fw-v0.5"), Ok(()));
        assert_eq!(
            c.fetch("fw-v0.9"),
            Err("fw-v0.9 has no firmware archive.".into())
        );
    }

    /// Writes a gzipped tar at `path` holding `members` (name, bytes) as
    /// regular files, names written raw so unsafe ones can be tested.
    fn tarball(path: &Path, members: &[(&str, Vec<u8>)]) {
        let gz = flate2::write::GzEncoder::new(
            std::fs::File::create(path).unwrap(),
            flate2::Compression::fast(),
        );
        let mut b = tar::Builder::new(gz);
        for (name, data) in members {
            let mut h = tar::Header::new_gnu();
            h.set_size(data.len() as u64);
            h.set_mode(0o644);
            h.set_entry_type(tar::EntryType::Regular);
            // set_path refuses "..", so write the raw name for odd members.
            h.as_old_mut().name[..name.len()].copy_from_slice(name.as_bytes());
            h.set_cksum();
            b.append(&h, data.as_slice()).unwrap();
        }
        b.into_inner().unwrap().finish().unwrap();
    }

    #[test]
    fn install_extracts_verifies_and_marks() {
        let dir = tempfile::tempdir().unwrap();
        let elf = testelf::build(
            &[(0x0800_0000, vec![1, 2, 3, 4], 4)],
            &[("__tag_flash_page_size__", 4096)],
        );
        let elf_file = dir.path().join("BitTag.elf");
        std::fs::write(&elf_file, &elf).unwrap();
        let manifest = serde_json::json!({"artifacts": {"BitTag.elf": {
            "sha256": sha256_of_file(&elf_file), "bytes": elf.len()}}, "source": {"commit": "abc"}});

        let cache = dir.path().join("cache");
        let c = Catalog::new(&cache);
        let staging = cache.join("fw-v0.6.partial");
        std::fs::create_dir_all(&staging).unwrap();
        let archive = staging.join("archive.tar.gz");
        tarball(
            &archive,
            &[
                ("./firmware/BitTag/BitTag.elf", elf.clone()),
                (
                    "firmware/BitTag/BitTag-build-manifest.json",
                    manifest.to_string().into_bytes(),
                ),
                ("firmware/BitTag/BitTag.bin", vec![0; 4]),
                ("../evil/firmware/BitTag/BitTag.elf", vec![0; 4]),
                ("README", vec![0; 4]),
            ],
        );
        c.install("fw-v0.6", &archive, &staging).unwrap();
        assert!(c.is_cached("fw-v0.6"));
        assert!(!staging.exists());
        assert!(!cache.join("fw-v0.6/firmware/BitTag/BitTag.bin").exists());
        assert!(!dir.path().join("evil").exists());
        assert_eq!(c.targets("fw-v0.6"), ["BitTag"]);
        assert!(c.images("fw-v0.6")[0].manifest_verified);

        // A bad image refuses the whole release.
        std::fs::create_dir_all(&staging).unwrap();
        tarball(&archive, &[("firmware/PresTag/PresTag.elf", elf.clone())]);
        let err = c.install("fw-v0.7", &archive, &staging).unwrap_err();
        assert_eq!(
            err,
            "The release has no PresTag-build-manifest.json beside PresTag.elf, so the image cannot be checked."
        );
        assert!(!staging.exists() && !c.is_cached("fw-v0.7"));

        std::fs::create_dir_all(&staging).unwrap();
        tarball(&archive, &[("README", vec![0; 4])]);
        assert_eq!(
            c.install("fw-v0.7", &archive, &staging).unwrap_err(),
            "The archive holds no firmware images."
        );
    }
}

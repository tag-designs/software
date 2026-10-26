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
const USER_AGENT: &str = "tagprogram";
const VERIFIED_MARKER: &str = ".verified";
const LISTING_FILE: &str = "releases.json";

static FIRMWARE_TAG: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"^fw-v\d+(\.\d+)*$").unwrap());
static QUALIFICATION: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^qualification-([A-Za-z0-9_]+)(-\d{8})?\.tar\.gz$").unwrap());
// The regex crate has no backreferences, so the `\1` of qtprogram's pattern
// is checked by hand in `acceptable_member`.
static MEMBER: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^firmware/([A-Za-z0-9_-]+)/([A-Za-z0-9_-]+)(\.elf|-build-manifest\.json)$")
        .unwrap()
});

/// One published (or cached) firmware release.
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
/// `firmware/<T>/<T>-build-manifest.json` (after any leading `./`).
pub fn acceptable_member(name: &str) -> bool {
    MEMBER.captures(name).is_some_and(|c| c[1] == c[2])
}

fn version_of(tag: &str) -> Vec<u64> {
    tag.get(4..)
        .unwrap_or("")
        .split('.')
        .map_while(|n| n.parse().ok())
        .collect()
}

fn sort_by_version(releases: &mut [FirmwareRelease]) {
    releases.sort_by_key(|r| std::cmp::Reverse(version_of(&r.tag)));
}

fn parse_time(text: &str) -> Option<DateTime<Utc>> {
    DateTime::parse_from_rfc3339(text)
        .ok()
        .map(|t| t.with_timezone(&Utc))
}

/// Parses a GitHub releases listing: firmware releases with an archive,
/// drafts skipped, newest version first.
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
pub struct Catalog {
    cache_dir: PathBuf,
    releases: Mutex<Vec<FirmwareRelease>>,
    fetching: Mutex<Option<String>>,
}

impl Catalog {
    /// A catalog over `cache_dir` (created if missing), loaded from its
    /// `releases.json`.
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
    pub fn is_cached(&self, tag: &str) -> bool {
        is_firmware_tag(tag) && self.cache_dir.join(tag).join(VERIFIED_MARKER).exists()
    }

    /// The release being fetched, if any.
    pub fn fetching(&self) -> Option<String> {
        self.fetching.lock().unwrap().clone()
    }

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
    pub fn refresh(&self) -> Result<(), String> {
        let reply = Self::client(Duration::from_secs(30))?
            .get(RELEASES_URL)
            .header("Accept", "application/vnd.github+json")
            .send()
            .map_err(|e| e.to_string())?;
        let status = reply.status().as_u16();
        if status == 403 || status == 429 {
            return Err("GitHub refused the listing (rate limit); try again later.".into());
        }
        let reply = reply.error_for_status().map_err(|e| e.to_string())?;
        let listed = parse_releases(&reply.text().map_err(|e| e.to_string())?);
        let _ = std::fs::write(self.cache_dir.join(LISTING_FILE), listing_to_json(&listed));
        self.set_releases(listed);
        Ok(())
    }

    /// Downloads, extracts, verifies and installs release `tag` (blocking).
    ///
    /// Already cached: nothing to do. Refuses while another release is
    /// downloading, or when the release has no archive. Any failure removes
    /// the staging directory, so a release is either fully cached and
    /// verified or absent.
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
    pub fn images(&self, tag: &str) -> Vec<FirmwareImage> {
        self.targets(tag)
            .iter()
            .map(|t| load_firmware_image(&self.image_path(tag, t), Some(tag)))
            .collect()
    }
}

/// Names of the subdirectories of `dir`, sorted.
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::elf::testelf;
    use crate::model::image::sha256_of_file;

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

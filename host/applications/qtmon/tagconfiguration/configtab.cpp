/**
 * @file    configtab.cpp
 * @brief   ConfigTab: the panel table, building and showing panels, reading
 *          and writing Config, the document (new, open, save, reset), and
 *          Start.
 */

//#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QGroupBox>
#include <QAbstractButton>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QSettings>
#include <QScreen>
#include <QScrollBar>
// #include <QTextEdit>
// #include <QTime>
#include <QTimer>
// #include <QtWidgets/QSpacerItem>
// #include <QtWidgets/QSizePolicy>
// #include <ctime>
#include <algorithm>
#include <fstream>
#include <map>
#include <google/protobuf/util/json_util.h>
#include <streambuf>

#include "configtab.h"
#include "adxl362panel.h"
#include "bittaglogpanel.h"
#include "configshape.h"
#include "lsm6panel.h"
#include "schedulepanel.h"
#include "ui_configtab.h"
#include "qtfiledialog.h"

namespace
{

template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, int)
    -> decltype(options.always_print_fields_with_no_presence = value, void())
{
  options.always_print_fields_with_no_presence = value;
}

template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, long)
    -> decltype(options.always_print_primitive_fields = value, void())
{
  options.always_print_primitive_fields = value;
}

/**
 * @brief The configuration panels, in page order. Each is built once.
 */
enum PanelId
{
  IntervalSchedule,            ///< start, run, hibernation
  IntervalScheduleWithPeriod,  ///< the above plus PresTag's sampling period
  StartDelaySchedule,          ///< IMUTag's start delay
  BitTagLogFormat,             ///< BitTag log format
  Adxl362Standard,             ///< classic BitTag accelerometer
  Adxl362Wake,                 ///< BitTag LE and BitPresTag wake mode
  Adxl367Wake,                 ///< UIUCTag ADXL367 wake-up
  Adxl367WakeWithType,         ///< BitTagNG: the above plus accel_type
  Lsm6Imu,                     ///< IMUTag LSM6DSV
  PanelCount
};

/**
 * @brief Which panels each tag type shows. A tag type with no entry shows
 *        none. `qtmonitor --check-config-panels` checks this table against
 *        every tag type's ConfigShape (its default configuration).
 */
const std::map<TagType, std::vector<PanelId>> &panelsForTag()
{
  static const std::map<TagType, std::vector<PanelId>> table = {
      {BITTAG, {IntervalSchedule, BitTagLogFormat, Adxl362Standard}},
      {BITTAG_LE, {IntervalSchedule, BitTagLogFormat, Adxl362Wake}},
      {BITPRESTAG, {IntervalSchedule, Adxl362Wake}},
      {BITTAGNG, {IntervalSchedule, Adxl367WakeWithType}},
      {UIUCTAG, {IntervalSchedule, Adxl367Wake}},
      {COMPASSTAG, {IntervalSchedule}},
      {PRESTAG, {IntervalScheduleWithPeriod}},
      {IMUTAG, {StartDelaySchedule, Lsm6Imu}},
  };
  return table;
}

/// True when @p a is @p b, or one is a field inside the other.
bool overlaps(std::string_view a, std::string_view b)
{
  auto under = [](std::string_view child, std::string_view parent) {
    return child.size() > parent.size() && child.substr(0, parent.size()) == parent &&
           child[parent.size()] == '.';
  };
  return a == b || under(a, b) || under(b, a);
}

} // namespace

ConfigTab::ConfigTab(QWidget *parent) : QWidget(parent)
{
  ui.setupUi(this);

  // initialize message box

  msgBox.setWindowTitle("Error");
  msgBox.setStandardButtons(QMessageBox::Ok);

  // Build every panel once, in page order, hidden until a tag selects it.
  panels_.resize(PanelCount);
  panels_[IntervalSchedule] = new SchedulePanel(SchedulePanel::Variant::Interval);
  panels_[IntervalScheduleWithPeriod] =
      new SchedulePanel(SchedulePanel::Variant::IntervalWithPeriod);
  panels_[StartDelaySchedule] = new SchedulePanel(SchedulePanel::Variant::StartDelay);
  panels_[BitTagLogFormat] = new BitTagLogPanel();
  panels_[Adxl362Standard] = new Adxl362Panel(Adxl362Panel::Variant::Standard, false);
  panels_[Adxl362Wake] = new Adxl362Panel(Adxl362Panel::Variant::Wake362, false);
  panels_[Adxl367Wake] = new Adxl362Panel(Adxl362Panel::Variant::Wake367, false);
  panels_[Adxl367WakeWithType] = new Adxl362Panel(Adxl362Panel::Variant::Wake367, true);
  panels_[Lsm6Imu] = new Lsm6Panel();

  static const char *const names[PanelCount] = {
      "IntervalSchedule", "IntervalScheduleWithPeriod", "StartDelaySchedule",
      "BitTagLogFormat", "Adxl362Standard", "Adxl362Wake", "Adxl367Wake",
      "Adxl367WakeWithType", "Lsm6Imu"};
  for (int i = 0; i < PanelCount; ++i)
  {
    panels_[i]->setObjectName(names[i]);
    panels_[i]->setVisible(false);
    ui.panelLayout->addWidget(panels_[i]);
  }
  ui.panelLayout->addStretch(1);
  ui.panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  placeholder_ = new QLabel(
      tr("No configuration.\n\nUse File \u25B8 New Configuration for a tag type's "
         "defaults, File \u25B8 Open Configuration for a saved file, or attach a "
         "tag to read its configuration."));
  placeholder_->setAlignment(Qt::AlignCenter);
  placeholder_->setWordWrap(true);
  placeholder_->setStyleSheet("color: palette(mid);");
  ui.panelLayout->insertWidget(0, placeholder_);

  // Any control's change re-checks whether the document is modified.
  for (QAbstractButton *b : findChildren<QAbstractButton *>())
    connect(b, &QAbstractButton::toggled, this, &ConfigTab::controlChanged);
  for (QSpinBox *b : findChildren<QSpinBox *>())
    connect(b, &QSpinBox::valueChanged, this, &ConfigTab::controlChanged);
  for (QDoubleSpinBox *b : findChildren<QDoubleSpinBox *>())
    connect(b, &QDoubleSpinBox::valueChanged, this, &ConfigTab::controlChanged);
  for (QDateTimeEdit *b : findChildren<QDateTimeEdit *>())
    connect(b, &QDateTimeEdit::dateTimeChanged, this, &ConfigTab::controlChanged);
}

ConfigTab::~ConfigTab(){}

bool ConfigTab::isModified() const
{
  if (!hasDocument())
    return false;
  Config now;
  return !collect(now) || now.SerializeAsString() != baseline_;
}

QString ConfigTab::documentName() const
{
  const QString type = QString::fromStdString(std::string(TagType_Name(tag_type_)));
  switch (source_)
  {
  case Source::None:
    return {};
  case Source::Defaults:
    return tr("%1 defaults").arg(type);
  case Source::File:
    return QFileInfo(path_).fileName();
  case Source::Tag:
    return tr("%1 from tag").arg(type);
  }
  return {};
}

void ConfigTab::Clear()
{
  for (ConfigPanel *panel : panels_)
    panel->setVisible(false);
  shown_.clear();
  current_config_.Clear();
  tag_type_ = TAG_UNSPECIFIED;
  source_ = Source::None;
  path_.clear();
  baseline_.clear();
  placeholder_->setVisible(true);
  was_modified_ = false;
  emit documentChanged();
}

bool ConfigTab::confirmDiscard()
{
  if (!isModified())
    return true;
  QMessageBox box(this);
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(tr("Unsaved Configuration"));
  box.setText(tr("The configuration \"%1\" has unsaved changes.").arg(documentName()));
  box.setInformativeText(tr("Save them before continuing?"));
  box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  box.setDefaultButton(QMessageBox::Save);
  switch (box.exec())
  {
  case QMessageBox::Save:
    return Save();
  case QMessageBox::Discard:
    return true;
  default:
    return false;
  }
}

void ConfigTab::controlChanged()
{
  if (loading_ || !hasDocument())
    return;
  const bool modified = isModified();
  if (modified != was_modified_)
  {
    was_modified_ = modified;
    emit documentChanged();
  }
}

void ConfigTab::updateEditable()
{
  const bool running_tags_own =
      source_ == Source::Tag && attached_ && old_state_ != IDLE;
  for (ConfigPanel *panel : panels_)
    panel->setEnabled(!running_tags_own &&
                      std::find(shown_.begin(), shown_.end(), panel) != shown_.end());
}

/**
 * @brief Enables or disables display-only fixture behavior.
 *
 * @details In fixture mode, StateUpdate() still enables controls according to
 *          tag state so screenshots look like live qtmonitor, but Start and
 *          Read return before using the live Tag pointer.
 *
 * @param[in] enabled   true when the tab is backed by fixture data.
 */
void ConfigTab::SetFixtureMode(bool enabled)
{
  fixture_mode_ = enabled;
}

/**
 * @brief Checks the panel table against every tag type's ConfigShape.
 *
 * @details For each tag type the table lists, every field a selected panel
 *          owns must be in the tag's shape, so no panel edits a field the tag
 *          lacks; and every field in the shape must be owned by a selected
 *          panel, or be inside or around one it owns, so the default config
 *          lists nothing qtmonitor ignores. Every tag type with a default
 *          config must have a table entry, and every entry a default config.
 *
 * @return One line per mismatch; empty when the panels and shapes agree.
 */
QStringList ConfigTab::CheckPanelsAgainstShapes() const
{
  QStringList problems;
  const auto &table = panelsForTag();
  for (const auto &[tag_type, ids] : table)
  {
    const QString tag = QString::fromStdString(std::string(TagType_Name(tag_type)));
    const ConfigShape &shape = ConfigShape::forTag(tag_type);
    if (shape.paths().empty())
    {
      problems << tag + ": has panels but no default configuration";
      continue;
    }
    std::vector<std::string_view> owned;
    for (PanelId id : ids)
    {
      for (std::string_view path : panels_[id]->paths())
      {
        owned.push_back(path);
        if (!ConfigShape::isConfigPath(path))
          problems << QString("%1: panel %2 owns \"%3\", which is not a Config field")
                          .arg(tag, panels_[id]->objectName(),
                               QString::fromUtf8(path.data(), int(path.size())));
        else if (!shape.paths().count(path))
          problems << QString("%1: panel %2 edits %3, which the tag's default "
                              "configuration does not list")
                          .arg(tag, panels_[id]->objectName(),
                               QString::fromUtf8(path.data(), int(path.size())));
      }
    }
    for (const std::string &path : shape.paths())
    {
      bool covered = false;
      for (std::string_view o : owned)
        covered = covered || overlaps(path, o);
      if (!covered)
        problems << QString("%1: no panel edits %2, which the tag's default "
                            "configuration lists")
                        .arg(tag, QString::fromStdString(path));
    }
  }

  const auto *types = TagType_descriptor();
  for (int i = 0; i < types->value_count(); ++i)
  {
    const auto tag_type = static_cast<TagType>(types->value(i)->number());
    if (!ConfigShape::forTag(tag_type).paths().empty() && !table.count(tag_type))
      problems << QString::fromStdString(std::string(types->value(i)->name())) +
                      ": has a default configuration but no panels";
  }
  for (const std::string &error : ConfigShape::loadErrors())
    problems << QString::fromStdString(error);
  return problems;
}

bool ConfigTab::Attach(Tag &t)
{
  tag = &t;
  return true;
}

void ConfigTab::Detach()
{
  tag = nullptr;
  attached_ = false;
  updateEditable();
}

void ConfigTab::StateUpdate(TagState state)
{
  attached_ = true;
  old_state_ = state;
  updateEditable();
}

/*****************************************************
 *                Configuration TAB                  *
 ****************************************************/

bool ConfigTab::collect(Config &config) const
{
  if (!hasDocument())
    return false;
  config = current_config_;
  config.set_tag_type(tag_type_);

  // Each shown panel writes the fields it owns; everything else passes
  // through from the document.
  for (ConfigPanel *panel : shown_)
  {
    if (!panel->store(config))
      return false;
  }
  return true;
}

bool ConfigTab::GetConfig(Config &config)
{
  if (!collect(config))
  {
    qDebug() << "GetConfig: no document, or a panel holds a value it cannot write";
    return false;
  }
  current_config_ = config;
  return true;
}

bool ConfigTab::SetConfig(const Config &new_config)
{
  load(new_config, Source::Tag, QString());
  return true;
}

void ConfigTab::load(const Config &config, Source source, const QString &path,
                     bool keep_baseline)
{
  loading_ = true;
  current_config_ = config;
  tag_type_ = config.tag_type();

  // Show the panels the tag type selects; each loads its own fields.
  shown_.clear();
  std::vector<PanelId> selected;
  const auto it = panelsForTag().find(tag_type_);
  if (it != panelsForTag().end())
    selected = it->second;
  for (int i = 0; i < PanelCount; ++i)
  {
    const bool wanted =
        std::find(selected.begin(), selected.end(), PanelId(i)) != selected.end();
    const bool applies = wanted && panels_[i]->load(config);
    panels_[i]->setVisible(applies);
    if (applies)
      shown_.push_back(panels_[i]);
  }
  placeholder_->setVisible(false);
  source_ = source;
  path_ = path;
  loading_ = false;

  if (!keep_baseline)
  {
    Config written;
    baseline_ = collect(written) ? written.SerializeAsString() : std::string();
  }
  was_modified_ = isModified();
  updateEditable();
  FitPanelPage();
  setVisible(true);
  setEnabled(true);
  emit documentChanged();
}

/**
 * @brief Sizes the scroll area to the shown panels.
 *
 * @details QScrollArea's own size hint is capped at a few hundred pixels, so
 *          without this the window would open short and scroll a page that
 *          fits the screen. The area is made at least as wide as the page's
 *          minimum, so it never needs a horizontal scroll bar, and as tall as
 *          the page up to the screen's
 *          available height less room for the rest of the window; a taller
 *          page scrolls vertically.
 */
void ConfigTab::FitPanelPage()
{
  ui.panelPage->adjustSize();
  const QSize page = ui.panelPage->sizeHint();
  const int scrollbar = ui.panelScroll->verticalScrollBar()->sizeHint().width();
  ui.panelScroll->setMinimumWidth(ui.panelPage->minimumSizeHint().width() + scrollbar);

  int limit = page.height();
  if (const QScreen *s = screen())
    limit = std::min(limit, s->availableGeometry().height() - kWindowChrome);
  ui.panelScroll->setMinimumHeight(std::max(limit, 0));
}

/*
 * The document: new, open, save, reset
 */

namespace
{
/// QSettings key and length of the recent configuration files.
constexpr const char *kRecentKey = "recentConfigurationFiles";
constexpr int kRecentMax = 10;
} // namespace

QStringList ConfigTab::recentFiles()
{
  return QSettings().value(kRecentKey).toStringList();
}

void ConfigTab::addRecent(const QString &path)
{
  const QString full = QFileInfo(path).absoluteFilePath();
  QStringList files = recentFiles();
  files.removeAll(full);
  files.prepend(full);
  while (files.size() > kRecentMax)
    files.removeLast();
  QSettings().setValue(kRecentKey, files);
}

bool ConfigTab::LoadDefaults(TagType tag_type)
{
  const std::optional<Config> defaults = ConfigShape::defaultConfig(tag_type);
  if (!defaults)
    return false;
  load(*defaults, Source::Defaults, QString());
  return true;
}

void ConfigTab::NewFromDefaults(TagType tag_type)
{
  if (!confirmDiscard())
    return;
  LoadDefaults(tag_type);
}

void ConfigTab::ResetToDefaults()
{
  const std::optional<Config> defaults = ConfigShape::defaultConfig(tag_type_);
  if (!hasDocument() || !defaults)
    return;
  // The values change; the document (its source and file) does not, so the
  // reset shows as unsaved changes.
  load(*defaults, source_, path_, true);
}

ConfigTab::OpenResult ConfigTab::OpenFromJsonText(const std::string &json_text,
                                                  TagType fallback_type,
                                                  const QString &path,
                                                  QString *detail)
{
  Config parsed;
  const auto status = google::protobuf::util::JsonStringToMessage(json_text, &parsed);
  if (!status.ok())
  {
    if (detail)
      *detail = QString::fromStdString(status.ToString());
    return OpenResult::ParseError;
  }
  std::string shape_error;
  const std::optional<ConfigShape> file_shape =
      ConfigShape::forFile(json_text, &shape_error);
  if (!file_shape)
  {
    if (detail)
      *detail = QString::fromStdString(shape_error);
    return OpenResult::ParseError;
  }

  const TagType type =
      parsed.tag_type() != TAG_UNSPECIFIED ? parsed.tag_type() : fallback_type;
  std::optional<Config> document = ConfigShape::defaultConfig(type);
  if (!document)
  {
    if (detail)
      *detail = tr("No default configuration for tag type %1")
                    .arg(QString::fromStdString(std::string(TagType_Name(type))));
    return OpenResult::NoDefaults;
  }
  ConfigShape::forTag(type).merge(*document, parsed, *file_shape);
  load(*document, Source::File, path);
  return OpenResult::Opened;
}

void ConfigTab::Open()
{
  if (!confirmDiscard())
    return;
  const QString start = path_.isEmpty() ? QDir::homePath() : QFileInfo(path_).path();
  const QString fileName = HostFileDialog::getOpenFileName(
      this, tr("Open Configuration"), start,
      tr("Configuration (*.json);;All Files (*)"));
  if (!fileName.isEmpty())
    OpenFile(fileName);
}

bool ConfigTab::OpenFile(const QString &path)
{
  if (!confirmDiscard())
    return false;

  std::ifstream fin(path.toStdString());
  if (!fin.is_open())
  {
    QMessageBox::warning(this, tr("Open Configuration"), tr("Couldn't open %1").arg(path));
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(fin)),
                         std::istreambuf_iterator<char>());

  // A file that names no tag type: ask, offering the attached tag's first.
  TagType fallback = TAG_UNSPECIFIED;
  Config probe;
  if (google::protobuf::util::JsonStringToMessage(text, &probe).ok() &&
      probe.tag_type() == TAG_UNSPECIFIED)
  {
    QStringList names;
    std::vector<TagType> types = ConfigShape::tagTypes();
    const TagType attached_type = attached_ ? attached_tag_type_ : TAG_UNSPECIFIED;
    std::stable_partition(types.begin(), types.end(),
                          [attached_type](TagType t) { return t == attached_type; });
    for (TagType t : types)
      names << QString::fromStdString(std::string(TagType_Name(t)));
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, tr("Open Configuration"),
        tr("%1 does not say which tag type it is for. Open it as:")
            .arg(QFileInfo(path).fileName()),
        names, 0, false, &ok);
    if (!ok)
      return false;
    fallback = types[static_cast<size_t>(names.indexOf(chosen))];
  }

  QString detail;
  switch (OpenFromJsonText(text, fallback, QFileInfo(path).absoluteFilePath(), &detail))
  {
  case OpenResult::Opened:
    addRecent(path);
    return true;
  case OpenResult::ParseError:
    QMessageBox::warning(this, tr("Open Configuration"),
                         tr("Couldn't read %1\n\n%2").arg(path, detail));
    return false;
  case OpenResult::NoDefaults:
    QMessageBox::warning(this, tr("Open Configuration"),
                         tr("Couldn't open %1\n\n%2").arg(path, detail));
    return false;
  }
  return false;
}

bool ConfigTab::writeFile(const QString &path)
{
  Config config;
  if (!GetConfig(config))
  {
    QMessageBox::warning(this, tr("Save Configuration"),
                         tr("The configuration cannot be written: a setting has no "
                            "valid value."));
    return false;
  }
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  setAlwaysPrintDefaultFields(options, true, 0);
  options.preserve_proto_field_names = true;

  std::string json_string;
  if (!MessageToJsonString(config, &json_string, options).ok())
  {
    QMessageBox::warning(this, tr("Save Configuration"), tr("Couldn't create JSON output"));
    return false;
  }
  std::ofstream fs(path.toStdString());
  if (!fs.is_open())
  {
    QMessageBox::warning(this, tr("Save Configuration"), tr("Couldn't open %1").arg(path));
    return false;
  }
  fs << json_string;
  fs.close();
  if (fs.fail())
  {
    QMessageBox::warning(this, tr("Save Configuration"), tr("Couldn't write %1").arg(path));
    return false;
  }

  // The saved file is now the document, unchanged.
  source_ = Source::File;
  path_ = QFileInfo(path).absoluteFilePath();
  baseline_ = config.SerializeAsString();
  was_modified_ = false;
  addRecent(path_);
  updateEditable();
  emit documentChanged();
  return true;
}

bool ConfigTab::Save()
{
  if (!hasDocument())
    return false;
  if (source_ == Source::File && !path_.isEmpty())
    return writeFile(path_);
  return SaveAs();
}

bool ConfigTab::SaveAs()
{
  if (!hasDocument())
    return false;
  const QString suggested =
      !path_.isEmpty()
          ? path_
          : QDir::home().filePath(
                QString::fromStdString(std::string(TagType_Name(tag_type_))).toLower() +
                "-config.json");
  const QString fileName = HostFileDialog::getSaveFileName(
      this, tr("Save Configuration As"), suggested,
      tr("Configuration (*.json);;All Files (*)"));
  if (fileName.isEmpty())
    return false;
  return writeFile(fileName);
}

void ConfigTab::StartTag()
{
  if (fixture_mode_) {
    qInfo() << "Ignoring Start in qtmonitor fixture display mode";
    return;
  }

  Config config;
  if (!GetConfig(config)) {
    qDebug() << "StartTag failed to get config";
    return;
  }

  if (tag == nullptr)
    return;
  qDebug().noquote() << "Starting tag with config:"
                     << QString::fromStdString(config.DebugString());
  std::string message;
  if (!tag->Start(config))
  {
    message = tag->DebugMessage();
  }
  pollStartResult(message, 0);
}

namespace {
constexpr int kStartPollAttempts = 8;
constexpr int kStartPollIntervalMs = 1000;
}  // namespace

void ConfigTab::pollStartResult(std::string start_call_message, int attempt)
{
  Status status;
  if (tag->GetStatus(status))
  {
    old_state_ = status.state();
    if (old_state_ == RUNNING || old_state_ == CONFIGURED)
    {
      // The tag reached the expected post-Start state, even if the
      // original Start() call itself reported a transient failure -- see
      // pollStartResult()'s doc comment. Nothing to show the user; just
      // note the swallowed error for anyone reading logs.
      if (!start_call_message.empty())
      {
        qWarning().noquote()
            << "Start() reported a transient failure but the tag reached"
            << QString::fromStdString(TagState_Name(old_state_)) << ":"
            << QString::fromStdString(start_call_message);
      }
      return;
    }
  }

  if (attempt + 1 < kStartPollAttempts)
  {
    QTimer::singleShot(kStartPollIntervalMs, this,
                       [this, start_call_message, attempt]() {
                         pollStartResult(start_call_message, attempt + 1);
                       });
    return;
  }

  QMessageBox startFailedBox;
  startFailedBox.setWindowTitle("Error");
  startFailedBox.setIcon(QMessageBox::Warning);
  startFailedBox.setText("Start Failed");
  startFailedBox.setStandardButtons(QMessageBox::Ok);
  if (!start_call_message.empty())
  {
    QString info = QString::fromStdString(start_call_message);
    info += "\nLast known state: ";
    info += QString::fromStdString(TagState_Name(old_state_));
    startFailedBox.setInformativeText(info);
    startFailedBox.setDetailedText(QString::fromStdString(start_call_message));
  }
  else
  {
    startFailedBox.setInformativeText(
        "Last known state: " +
        QString::fromStdString(TagState_Name(old_state_)));
  }
  startFailedBox.exec();
}

void ConfigTab::ReadFromTag()
{
  if (fixture_mode_) {
    qInfo() << "Ignoring Read in qtmonitor fixture display mode";
    return;
  }

  if (tag == nullptr || !confirmDiscard())
    return;
  Config configin;
  if (tag->GetConfig(configin)) {
    SetConfig(configin);
  }
  else {
    msgBox.setText("Tag config read failed");
    msgBox.exec();
  }
}

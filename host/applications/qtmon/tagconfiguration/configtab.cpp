/**
 * @file    configtab.cpp
 * @brief   ConfigTab: the panel table, building and showing panels, reading
 *          and writing Config, file save and restore, and Start.
 */

//#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QGroupBox>
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
}

ConfigTab::~ConfigTab(){}

bool ConfigTab::isActive(){
  return active;
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
  for (ConfigPanel *panel : panels_)
    panel->setVisible(false);
  shown_.clear();
  setEnabled(false);
  setVisible(false);
  active = false;
}

void ConfigTab::StateUpdate(TagState state)
{
  old_state_ = state;
  const bool idle = state == IDLE;
  for (ConfigPanel *panel : panels_)
    panel->setEnabled(idle && std::find(shown_.begin(), shown_.end(), panel) != shown_.end());
  ui.configRestoreButton->setEnabled(idle);
  ui.startButton->setEnabled(idle);
  ui.readButton->setEnabled(idle);
}

/*****************************************************
 *                Configuration TAB                  *
 ****************************************************/

bool ConfigTab::GetConfig(Config &config)
{
  config = current_config_;
  config.set_tag_type(tag_type_);

  // Each shown panel writes the fields it owns; everything else passes
  // through from the tag's own configuration.
  for (ConfigPanel *panel : shown_)
  {
    if (!panel->store(config))
    {
      qDebug() << "failed to get" << panel->objectName() << "config";
      return false;
    }
  }

  current_config_ = config;
  return true;
}

bool ConfigTab::SetConfig(const Config &new_config)
{
  current_config_ = new_config;
  tag_type_ = new_config.tag_type();

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
    const bool applies = wanted && panels_[i]->load(new_config);
    panels_[i]->setVisible(applies);
    if (applies)
      shown_.push_back(panels_[i]);
  }

  FitPanelPage();
  active = true;
  setVisible(true);
  setEnabled(true);
  return active;
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
 * configuration file operations
*/

void ConfigTab::on_configSaveButton_clicked()
{
  QString fileName = HostFileDialog::getSaveFileName(
      this, tr("Save File"), QDir::homePath() + "/untitled.json",
      tr("Protobuf (*.json);;All Files (*)"));
  QString errormsg;

  if (fileName.isNull()) {
    return;
  }

  do
  {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    setAlwaysPrintDefaultFields(options, true, 0);
    options.preserve_proto_field_names = true;

    std::ofstream fs(fileName.toStdString());

    if (!fs.is_open())
    {
      errormsg = "Couldn't Open " + fileName;
      break;
    }

    Config configout;
    GetConfig(configout);

    std::string json_string;
    if (MessageToJsonString(configout, &json_string, options).ok())
    {
      fs << json_string;
      if (fs.bad())
      {
        errormsg = "Couldn't Write " + fileName;
      }
    }
    else
    {
      errormsg = "Couldn't Create JSON output ";
    }

    fs.close();
  } while (0);

  if (!errormsg.isEmpty())
  {
    QMessageBox msgBox;
    msgBox.setText(errormsg);
    msgBox.exec();
    qDebug() << errormsg;
  }
}

// Restore config from file

void ConfigTab::on_configRestoreButton_clicked()
{
  QString fileName = HostFileDialog::getOpenFileName(
      this, tr("Open File"), QDir::homePath(),
      tr("Protobuf (*.json);;All Files (*)"));

  if (fileName.isNull())
    return;

  std::ifstream fin(fileName.toStdString());

  if (!fin.is_open())
  {
    QMessageBox msgBox;
    msgBox.setText("Couldn't Open " + fileName);
    msgBox.exec();
    qDebug() << "Config file restore couldn't open " << fileName;
    return;
  }

  std::string str((std::istreambuf_iterator<char>(fin)),
                  std::istreambuf_iterator<char>());

  fin.close();
  QString detail;
  switch (RestoreFromJsonText(str, &detail))
  {
  case RestoreResult::Restored:
    break;
  case RestoreResult::NotValidForTag:
  {
    QMessageBox msgBox;
    msgBox.setText("Config file parsed but is not valid for this tag");
    msgBox.exec();
    qDebug().noquote() << "Config file restore parsed but SetConfig failed:"
                       << detail;
    break;
  }
  case RestoreResult::ParseError:
  {
    QMessageBox msgBox;
    msgBox.setText("Couldn't Read " + fileName + "\n\n" + detail);
    msgBox.exec();
    qDebug().noquote() << "Config file restore couldn't read" << fileName
                       << detail;
    break;
  }
  }
}

/*
 * RestoreFromJsonText() (documented in configtab.h): parses the text as a
 * Config, copies into the current configuration each field that the attached
 * tag supports and the file speaks for (ConfigShape::forFile() and merge(): a
 * stated zero is copied, a field the tag lacks is not, and a file written for
 * another tag type contributes only that type's fields), and applies the
 * result with SetConfig(). On ParseError the current configuration is left
 * untouched. Shows no dialogs; on_configRestoreButton_clicked() reports the
 * outcome, and the configuration probe records it.
 */
ConfigTab::RestoreResult ConfigTab::RestoreFromJsonText(const std::string &json_text,
                                                        QString *detail)
{
  Config configin;
  google::protobuf::util::JsonParseOptions options2;
  auto status = JsonStringToMessage(json_text, &configin, options2);
  if (!status.ok())
  {
    if (detail)
      *detail = QString::fromStdString(status.ToString());
    return RestoreResult::ParseError;
  }

  std::string shape_error;
  const std::optional<ConfigShape> file_shape =
      ConfigShape::forFile(json_text, &shape_error);
  if (!file_shape)
  {
    if (detail)
      *detail = QString::fromStdString(shape_error);
    return RestoreResult::ParseError;
  }

  Config merged = current_config_;
  ConfigShape::forTag(merged.tag_type()).merge(merged, configin, *file_shape);
  if (!SetConfig(merged))
  {
    if (detail)
      *detail = QString::fromStdString(merged.DebugString());
    return RestoreResult::NotValidForTag;
  }
  return RestoreResult::Restored;
}

void ConfigTab::on_startButton_clicked()
{
  if (fixture_mode_) {
    qInfo() << "Ignoring Start in qtmonitor fixture display mode";
    return;
  }

  Config config;
  if (!GetConfig(config)) {
    qDebug() << "on_startButton_clicked failed to get config";
    return;
  }

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

void ConfigTab::on_readButton_clicked()
{
  if (fixture_mode_) {
    qInfo() << "Ignoring Read in qtmonitor fixture display mode";
    return;
  }

  Config configin;
  if (tag->GetConfig(configin)) {
    //qDebug()<< QString(configin.DebugString());
    SetConfig(configin);
  }
  else {
    msgBox.setText("Tag config read failed");
    msgBox.exec();
  }
  qDebug()<< "readButton clicked!";
}

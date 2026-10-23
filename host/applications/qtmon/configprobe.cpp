/**
 * @file    configprobe.cpp
 * @brief   Implementation of `qtmonitor --config-probe`.
 *
 * @details Each case runs through a freshly built ConfigTab, as a qtmonitor
 *          that has just attached this tag would, which also keeps one case's
 *          widget state out of the next.
 */

#include "configprobe.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <google/protobuf/util/json_util.h>

#include "tagconfiguration/configclock.h"
#include "tagconfiguration/configpanel.h"
#include "tagconfiguration/configtab.h"

namespace
{

/// Result and case file format identifiers, checked on read and written out.
constexpr const char *kCasesSchema = "qtmon-config-probe-cases/1";
constexpr const char *kResultsSchema = "qtmon-config-probe-results/1";

/**
 * @brief Sets the protobuf JSON option that prints zero-valued fields, under
 *        whichever name this protobuf version uses.
 */
template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, int)
    -> decltype(options.always_print_fields_with_no_presence = value, void())
{
  options.always_print_fields_with_no_presence = value;
}

/// @copydoc setAlwaysPrintDefaultFields
template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, long)
    -> decltype(options.always_print_primitive_fields = value, void())
{
  options.always_print_primitive_fields = value;
}

/**
 * @brief Converts a Config to a JSON object with every field printed.
 *
 * @details Field names are the .proto names, and zero values are included, so
 *          two results differ only where the sent configurations differ.
 */
QJsonObject configToJson(const Config &config)
{
  google::protobuf::util::JsonPrintOptions options;
  options.preserve_proto_field_names = true;
  setAlwaysPrintDefaultFields(options, true, 0);
  std::string text;
  if (!google::protobuf::util::MessageToJsonString(config, &text, options).ok())
    return QJsonObject{{"error", "MessageToJsonString failed"}};
  return QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
}

/**
 * @brief Number formatting used throughout the fingerprint: up to nine
 *        significant digits, enough to tell any two floats apart.
 */
QString num(double value)
{
  return QString::number(value, 'g', 9);
}

/**
 * @brief Reports whether a widget would be on screen, whatever is scrolled
 *        into view or selected.
 *
 * @details Walks from @p w up to @p root. Any explicitly hidden widget on the
 *          way hides @p w, except a QTabWidget page, which is hidden only
 *          because another tab is selected; such a page counts as shown when
 *          its tab itself is visible.
 */
bool shownIgnoringTabSelection(const QWidget *w, const QWidget *root)
{
  for (const QWidget *x = w; x != nullptr && x != root; x = x->parentWidget())
  {
    if (!x->isHidden())
      continue;
    const auto *stack = qobject_cast<const QStackedWidget *>(x->parentWidget());
    const auto *tabs = stack ? qobject_cast<const QTabWidget *>(stack->parentWidget())
                             : nullptr;
    if (tabs == nullptr)
      return false;
    const int index = tabs->indexOf(const_cast<QWidget *>(x));
    if (index < 0 || !tabs->isTabVisible(index))
      return false;
  }
  return true;
}

/**
 * @brief Titles of the group boxes enclosing @p w, outermost first.
 */
QString groupPath(const QWidget *w, const QWidget *root)
{
  QStringList titles;
  for (const QWidget *x = w->parentWidget(); x != nullptr && x != root;
       x = x->parentWidget())
  {
    if (const auto *box = qobject_cast<const QGroupBox *>(x))
      titles.prepend(box->title());
  }
  return titles.join(" > ");
}

/**
 * @brief Finds the QFormLayout row label for @p field, searching nested
 *        layouts of its parent widget.
 */
QString formLabelIn(const QLayout *layout, const QWidget *field)
{
  if (layout == nullptr)
    return {};
  if (const auto *form = qobject_cast<const QFormLayout *>(layout))
  {
    if (QWidget *label = form->labelForField(const_cast<QWidget *>(field)))
    {
      if (const auto *text = qobject_cast<const QLabel *>(label))
        return text->text();
    }
  }
  for (int i = 0; i < layout->count(); ++i)
  {
    const QString found = formLabelIn(layout->itemAt(i)->layout(), field);
    if (!found.isEmpty())
      return found;
  }
  return {};
}

/// @copydoc formLabelIn
QString formLabel(const QWidget *field)
{
  const QWidget *parent = field->parentWidget();
  return parent ? formLabelIn(parent->layout(), field) : QString();
}

/**
 * @brief Describes one control, or returns an empty string for a widget that
 *        carries nothing a user can see or set.
 */
QString describe(const QWidget *w)
{
  QStringList parts;
  if (const auto *box = qobject_cast<const QGroupBox *>(w))
  {
    parts << "group" << QString("title=\"%1\"").arg(box->title());
    if (box->isCheckable())
      parts << QString("checked=%1").arg(box->isChecked());
  }
  else if (const auto *label = qobject_cast<const QLabel *>(w))
  {
    if (label->text().isEmpty())
      return {};
    parts << "label" << QString("text=\"%1\"").arg(label->text());
  }
  else if (const auto *radio = qobject_cast<const QRadioButton *>(w))
  {
    parts << "radio" << QString("text=\"%1\"").arg(radio->text())
          << QString("checked=%1").arg(radio->isChecked());
  }
  else if (const auto *check = qobject_cast<const QCheckBox *>(w))
  {
    parts << "check" << QString("text=\"%1\"").arg(check->text())
          << QString("checked=%1").arg(check->isChecked());
  }
  else if (const auto *push = qobject_cast<const QPushButton *>(w))
  {
    parts << "button" << QString("text=\"%1\"").arg(push->text());
  }
  else if (const auto *dspin = qobject_cast<const QDoubleSpinBox *>(w))
  {
    parts << "double" << QString("label=\"%1\"").arg(formLabel(w))
          << "value=" + num(dspin->value()) << "min=" + num(dspin->minimum())
          << "max=" + num(dspin->maximum()) << "step=" + num(dspin->singleStep())
          << QString("decimals=%1").arg(dspin->decimals())
          << QString("prefix=\"%1\"").arg(dspin->prefix())
          << QString("suffix=\"%1\"").arg(dspin->suffix());
  }
  else if (const auto *spin = qobject_cast<const QSpinBox *>(w))
  {
    parts << "int" << QString("label=\"%1\"").arg(formLabel(w))
          << QString("value=%1").arg(spin->value())
          << QString("min=%1").arg(spin->minimum())
          << QString("max=%1").arg(spin->maximum())
          << QString("step=%1").arg(spin->singleStep())
          << QString("prefix=\"%1\"").arg(spin->prefix())
          << QString("suffix=\"%1\"").arg(spin->suffix());
  }
  else if (const auto *dt = qobject_cast<const QDateTimeEdit *>(w))
  {
    parts << "datetime" << QString("label=\"%1\"").arg(formLabel(w))
          << QString("value=%1").arg(dt->dateTime().toSecsSinceEpoch())
          << QString("min=%1").arg(dt->minimumDateTime().toSecsSinceEpoch())
          << QString("max=%1").arg(dt->maximumDateTime().toSecsSinceEpoch())
          << QString("format=\"%1\"").arg(dt->displayFormat());
  }
  else if (const auto *combo = qobject_cast<const QComboBox *>(w))
  {
    QStringList items;
    for (int i = 0; i < combo->count(); ++i)
      items << combo->itemText(i);
    parts << "combo" << QString("current=\"%1\"").arg(combo->currentText())
          << QString("items=\"%1\"").arg(items.join("|"));
  }
  else
  {
    return {};
  }

  parts << QString("enabled=%1").arg(w->isEnabled());
  if (!w->toolTip().isEmpty())
    parts << QString("tooltip=\"%1\"").arg(w->toolTip());
  return parts.join(' ');
}

/**
 * @brief Fingerprint of every visible control under @p root, sorted so that
 *        moving a control within the page does not change it.
 */
QJsonArray fingerprint(const QWidget *root)
{
  QStringList lines;
  for (const QWidget *w : root->findChildren<QWidget *>())
  {
    if (!shownIgnoringTabSelection(w, root))
      continue;
    const QString what = describe(w);
    if (what.isEmpty())
      continue;
    lines << groupPath(w, root) + " | " + what;
  }
  std::sort(lines.begin(), lines.end());
  return QJsonArray::fromStringList(lines);
}

/**
 * @brief Arrangement facts that are expected to change when the layout does:
 *        which panels are shown, any sub-tabs, and whether the tab as a whole
 *        is active.
 */
QJsonArray layoutFacts(const ConfigTab *tab)
{
  QStringList lines;
  for (const QWidget *w : tab->findChildren<QWidget *>())
  {
    const auto *panel = dynamic_cast<const ConfigPanel *>(w);
    if (panel && !panel->isHidden())
      lines << QString("panel %1").arg(panel->objectName());
  }
  for (const QTabWidget *tabs : tab->findChildren<QTabWidget *>())
  {
    for (int i = 0; i < tabs->count(); ++i)
      lines << QString("subtab \"%1\" visible=%2 enabled=%3")
                   .arg(tabs->tabText(i))
                   .arg(tabs->isTabVisible(i))
                   .arg(tabs->isTabEnabled(i));
  }
  lines << QString("configtab visible=%1 enabled=%2")
               .arg(!tab->isHidden())
               .arg(tab->isEnabled());
  return QJsonArray::fromStringList(lines);
}

/**
 * @brief Records and dismisses any modal message box, so a case that raises
 *        one does not block the probe.
 */
class DialogCatcher : public QObject
{
public:
  DialogCatcher()
  {
    timer_.setInterval(10);
    QObject::connect(&timer_, &QTimer::timeout, [this]() { poll(); });
    timer_.start();
  }

  /// Returns and clears the dialogs seen since the last call.
  QJsonArray take()
  {
    QJsonArray seen = seen_;
    seen_ = QJsonArray();
    return seen;
  }

private:
  void poll()
  {
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (box == nullptr)
      return;
    QString text = box->text();
    if (!box->informativeText().isEmpty())
      text += " / " + box->informativeText();
    seen_.append(text);
    box->done(0);
  }

  QTimer timer_;
  QJsonArray seen_;
};

/// What one pass through a ConfigTab produced.
struct RunOutput
{
  QString open;         ///< Open outcome, empty when the case opens no file.
  QString open_detail;
  QString document;     ///< The document's source, tag type and modified flag.
  bool get_config_ok = false;
  Config sent;
  QJsonArray ui;
  QJsonArray layout;
  QJsonArray dialogs;
};

/// One case's inputs.
struct CaseInput
{
  Config config;           ///< The tag's configuration, or the type for New.
  TagState state = IDLE;   ///< The tag's state, when attached.
  bool attached = true;    ///< false: no tag; the tab is used as an editor.
  bool new_document = false;  ///< true: New from config's tag type's defaults.
  QJsonValue open;         ///< Text of a file to open, or undefined.
};

/**
 * @brief Builds a fresh ConfigTab, applies one case to it, and reads back
 *        what Start would send.
 */
RunOutput runOnce(const CaseInput &in, DialogCatcher &dialogs)
{
  RunOutput out;
  ConfigTab tab;
  tab.SetFixtureMode(true);
  tab.resize(1000, 800);
  tab.show();
  QCoreApplication::processEvents();

  if (in.new_document)
    tab.LoadDefaults(in.config.tag_type());
  else
    tab.SetConfig(in.config);
  if (in.attached)
    tab.StateUpdate(in.state);

  if (in.open.isString())
  {
    QString detail;
    switch (tab.OpenFromJsonText(in.open.toString().toStdString(),
                                 in.config.tag_type(), QString(), &detail))
    {
    case ConfigTab::OpenResult::Opened:
      out.open = "Opened";
      break;
    case ConfigTab::OpenResult::ParseError:
      out.open = "ParseError";
      break;
    case ConfigTab::OpenResult::NoDefaults:
      out.open = "NoDefaults";
      break;
    }
    // Abseil varies the spacing of status messages from run to run so that
    // nothing depends on their exact text; collapse it for stable results.
    out.open_detail = detail.simplified();
  }

  QCoreApplication::processEvents();

  static const char *const sources[] = {"None", "Defaults", "File", "Tag"};
  out.document = QString("%1 %2 modified=%3")
                     .arg(sources[static_cast<int>(tab.source())])
                     .arg(QString::fromStdString(std::string(TagType_Name(tab.documentTagType()))))
                     .arg(tab.isModified());
  out.get_config_ok = tab.GetConfig(out.sent);
  out.ui = fingerprint(&tab);
  out.layout = layoutFacts(&tab);
  out.dialogs = dialogs.take();
  return out;
}

/**
 * @brief Runs one case and returns its result object.
 */
QJsonObject runCase(const QJsonObject &c, DialogCatcher &dialogs)
{
  QJsonObject result;
  result["id"] = c.value("id");

  Config input;
  const QByteArray config_text =
      QJsonDocument(c.value("config").toObject()).toJson(QJsonDocument::Compact);
  const auto parsed =
      google::protobuf::util::JsonStringToMessage(config_text.toStdString(), &input);
  if (!parsed.ok())
  {
    result["error"] = "case config does not parse: " +
                      QString::fromStdString(parsed.ToString());
    return result;
  }

  TagState state = IDLE;
  if (!TagState_Parse(c.value("state").toString("IDLE").toStdString(), &state))
  {
    result["error"] = "unknown state " + c.value("state").toString();
    return result;
  }

  ConfigClock::setFixedNow(static_cast<qint64>(c.value("now").toDouble()));
  CaseInput in;
  in.config = input;
  in.state = state;
  in.attached = c.value("attached").toBool(true);
  in.new_document = c.value("new").toBool(false);
  in.open = c.value("open");
  const RunOutput run = runOnce(in, dialogs);

  if (!run.open.isEmpty())
  {
    result["open"] = run.open;
    if (run.open != "Opened")
      result["open_detail"] = run.open_detail;
  }
  result["document"] = run.document;
  result["get_config_ok"] = run.get_config_ok;
  result["sent"] = configToJson(run.sent);
  result["sent_hex"] =
      QString::fromLatin1(QByteArray::fromStdString(run.sent.SerializeAsString()).toHex());
  result["ui"] = run.ui;
  result["layout"] = run.layout;
  if (!run.dialogs.isEmpty())
    result["dialogs"] = run.dialogs;
  return result;
}

/**
 * @brief Runs every case in one file and writes the matching result file.
 */
bool runCaseFile(const QString &path, const QDir &out, DialogCatcher &dialogs)
{
  QFile in(path);
  if (!in.open(QIODevice::ReadOnly))
  {
    std::fprintf(stderr, "config-probe: cannot open %s\n", qPrintable(path));
    return false;
  }
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject() ||
      doc.object().value("schema").toString() != kCasesSchema)
  {
    std::fprintf(stderr, "config-probe: %s is not a %s file\n", qPrintable(path),
                 kCasesSchema);
    return false;
  }

  QJsonArray results;
  for (const QJsonValue &c : doc.object().value("cases").toArray())
    results.append(runCase(c.toObject(), dialogs));

  QJsonObject root;
  root["schema"] = kResultsSchema;
  root["cases_file"] = QFileInfo(path).fileName();
  root["results"] = results;

  QFile file(out.filePath(QFileInfo(path).fileName()));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    std::fprintf(stderr, "config-probe: cannot write %s\n",
                 qPrintable(file.fileName()));
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  std::printf("config-probe: %s: %d cases\n", qPrintable(QFileInfo(path).fileName()),
              static_cast<int>(results.size()));
  return true;
}

} // namespace

int runConfigProbe(const QString &cases_path, const QString &out_dir)
{
  QStringList files;
  const QFileInfo info(cases_path);
  if (info.isDir())
  {
    for (const QString &name :
         QDir(cases_path).entryList({"*.json"}, QDir::Files, QDir::Name))
      files << QDir(cases_path).filePath(name);
  }
  else
  {
    files << cases_path;
  }
  if (files.isEmpty())
  {
    std::fprintf(stderr, "config-probe: no case files in %s\n", qPrintable(cases_path));
    return 1;
  }

  QDir out(out_dir);
  if (!out.mkpath("."))
  {
    std::fprintf(stderr, "config-probe: cannot create %s\n", qPrintable(out_dir));
    return 1;
  }

  DialogCatcher dialogs;
  bool ok = true;
  for (const QString &file : files)
    ok = runCaseFile(file, out, dialogs) && ok;
  ConfigClock::clearFixedNow();
  return ok ? 0 : 1;
}

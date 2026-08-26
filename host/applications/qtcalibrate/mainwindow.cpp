
#include <QMainWindow>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMenu>
#include <QPixmap>
#include <QSaveFile>
#include <QScreen>
#include <QSignalBlocker>
#include <QDateTime>

#include <QVector3D>

#include "tagclass.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qtfiledialog.h"

#include "sensorui_resources.h"
#include "tag.pb.h"

#include "txtlogs.h"


extern "C"
{
#include "log.h"
}

// main.cpp installs a Qt message handler that appends log output to this text
// edit. MainWindow sets it once the generated UI exists.

extern void myMessageOutput(QtMsgType type, const QMessageLogContext &context,
                            const QString &msg);
extern int log_level;
QTextEdit *s_textEdit = nullptr;

#define title_string "Tag Calibrator v0.1"

namespace
{

QJsonObject magToJson(const QVector3D &value)
{
  QJsonObject object;
  object["mx"] = value.x();
  object["my"] = value.y();
  object["mz"] = value.z();
  return object;
}

QJsonObject accelToJson(const QVector3D &value)
{
  QJsonObject object;
  object["ax"] = value.x();
  object["ay"] = value.y();
  object["az"] = value.z();
  return object;
}

QString resolveReplayCapturePath(const QString &path)
{
  QFileInfo requested(path);
  if (requested.exists() || requested.isAbsolute()) {
    return requested.filePath();
  }

  const QFileInfo sourceRelative(
      QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path));
  if (sourceRelative.exists()) {
    return sourceRelative.filePath();
  }

  return requested.filePath();
}

// Documentation commands are normally run from a build directory, while the
// fixture files and generated images live in the source tree. Resolve relative
// paths from TAG_DESIGNS_SOURCE_DIR so the commands work from either location.
QString resolveScreenshotDir(const QString &path)
{
  if (path.isEmpty()) {
    return QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR))
        .filePath("host/docs/src/images");
  }

  const QFileInfo requested(path);
  if (requested.isAbsolute()) {
    return requested.filePath();
  }

  return QDir(QStringLiteral(TAG_DESIGNS_SOURCE_DIR)).filePath(path);
}

// Qt Quick property changes are asynchronous enough that immediate screenshots
// can catch the previous convention or pose. A short local event loop lets QML
// repaint before the native window is grabbed.
void waitForGuiToSettle(int milliseconds)
{
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

} // namespace


MainWindow::MainWindow(const MainWindowOptions &options, QWidget *parent)
    : QMainWindow(parent) {
  ui.setupUi(this);
 
  this->setAttribute(Qt::WA_AlwaysShowToolTips, true);

  // Change Main Window title

  setWindowTitle(QString(title_string));
  captureReplayScreenshotsOnStartup = options.captureReplayScreenshots;
  captureStartupScreenshotOnStartup = options.captureStartupScreenshot;
  captureOrientationScreenshotOnStartup = options.captureOrientationScreenshot;
  screenshotDir = resolveScreenshotDir(options.screenshotDir);
  screenshotPrefix = options.screenshotPrefix.isEmpty()
                         ? QStringLiteral("qtcalibrate-collection")
                         : options.screenshotPrefix;
  orientationPose = options.orientationPose;

  // initialize logging window

  logWindowInit();
  orientationControlsInit();
  resetCalibrationDisplay();
  ui.actionSave_Sample_Capture->setEnabled(false);


  // Connect Timers to slots

  connect(&timer, SIGNAL(timeout()), this, SLOT(TriggerUpdate()));
  QObject::connect(&magnetic, &CompassData::calibration_update, this, &MainWindow::calibration_update);
  connect(&qualitytimer, SIGNAL(timeout()), this, SLOT(TriggerQualityUpdate()));

  // Connect ui

  //connect(ui.screenDirection,SIGNAL(valueChanged()),this, SLOT(on_screenDirection_valueChanged()));

  if (!options.replayCapturePath.isEmpty()) {
    if (loadReplayCapture(options.replayCapturePath)) {
      setupReplayTag();
    }
  } else if (options.skipAutoAttach) {
    Detach();
  } else {
    Attach();
  }

  // The compass and attitude views are shared sensorui QML resources. The
  // small C++ facades keep the rest of qtcalibrate independent of QML method
  // names and resource details.
  initializeSensorUiResources();
  ui.tagWidget->setSource(QUrl("qrc:/qfi/orientation_frame/MyCompass.qml"));
  qInfo() << ui.tagWidget->errors();
  ui.tagWidget->show();
  qInfo() << ui.tagWidget->errors();
  compassDisplay.setRootObject(ui.tagWidget->rootObject());
  compassDisplay.setDeclination(declinationDegrees);
  compassDisplay.setBatteryForward(batteryForward);

  ui.attitudeWidget->setSource(QUrl("qrc:/qfi/orientation_frame/MyAttitude.qml"));
  qInfo() << ui.attitudeWidget->errors();
  ui.attitudeWidget->show();
  qInfo() << ui.attitudeWidget->errors();
  attitudeDisplay.setRootObject(ui.attitudeWidget->rootObject());
  attitudeDisplay.setBatteryForward(batteryForward);

  if (replayEnabled && options.replayPercent >= 0) {
    prepareReplayMilestone(options.replayPercent);
  }
  
}

MainWindow::~MainWindow()
{
  tag.Detach();
}

bool MainWindow::shouldQuitAfterStartup() const
{
  return captureReplayScreenshotsOnStartup || captureStartupScreenshotOnStartup
      || captureOrientationScreenshotOnStartup;
}

bool MainWindow::loadReplayCapture(const QString &path)
{
  const QString resolvedPath = resolveReplayCapturePath(path);
  QFile file(resolvedPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Unable to open replay capture" << path << "resolved as"
               << resolvedPath;
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    qWarning() << "Unable to parse replay capture" << resolvedPath
               << parseError.errorString();
    return false;
  }

  const QJsonArray samples = document.object().value("samples").toArray();
  QVector<ReplaySample> loaded;
  loaded.reserve(samples.size());
  // Replay only needs the raw sensor stream. Calibration constants stored in
  // the capture remain useful metadata for humans, but the screenshot workflow
  // recomputes them through the same CompassData path used by live data.
  for (const QJsonValue &value : samples) {
    const QJsonObject object = value.toObject();
    const QJsonObject mag = object.value("mag").toObject();
    if (!mag.contains("mx") || !mag.contains("my") || !mag.contains("mz")) {
      continue;
    }

    ReplaySample sample;
    sample.mag = QVector3D(
        static_cast<float>(mag.value("mx").toDouble()),
        static_cast<float>(mag.value("my").toDouble()),
        static_cast<float>(mag.value("mz").toDouble()));
    sample.hasAccel = object.value("has_accel").toBool(false);
    if (sample.hasAccel) {
      const QJsonObject accel = object.value("accel").toObject();
      sample.accel = QVector3D(
          static_cast<float>(accel.value("ax").toDouble()),
          static_cast<float>(accel.value("ay").toDouble()),
          static_cast<float>(accel.value("az").toDouble()));
    }
    loaded.append(sample);
  }

  if (loaded.isEmpty()) {
    qWarning() << "Replay capture has no usable samples" << path;
    return false;
  }

  replaySamples = loaded;
  replayCapturePath = resolvedPath;
  replayCursor = 0;
  replayEnabled = true;
  qInfo() << "Loaded replay capture" << resolvedPath << "with"
          << replaySamples.size() << "samples";
  return true;
}

void MainWindow::setupReplayTag()
{
  // Replay mode looks attached to the UI but never talks to USB. This lets the
  // existing Start/Stop/Stream workflow drive both maintainer screenshots and
  // ad-hoc fixture validation.
  ui.attachButton->setEnabled(false);
  ui.detachButton->setEnabled(true);
  ui.streamCheckBox->setEnabled(true);
  ui.streamCheckBox->setChecked(false);
  ui.saveButton->setEnabled(false);
  ui.loadButton->setEnabled(false);

  magnetic.clear();
  ui.graphWidget->reset();
  resetCalibrationDisplay();
  clearSampleCapture();
  qInfo() << "Using replay capture instead of USB tag";
}

bool MainWindow::Attach()
{
  if (tag.IsAttached())
  {
    qInfo() << "Already attached to tag";
    return false;
  }

  // Find base

  std::vector<UsbDev> usbdevs;

  if (!tag.Available(usbdevs) || (usbdevs.size() == 0))
  {
    QMessageBox msgBox;
    msgBox.setWindowTitle("Warning");
    msgBox.setText("No Tag Bases Found");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.exec();
    qInfo() << "No Tag Bases Found";
    return false;
    //QTimer::singleShot(0, this, SLOT(close()));;
  }

  int index = 0;
  if (usbdevs.size() > 1)
  {
    QStringList items;

    for (int j = 0; j < usbdevs.size(); j++)
    {
      QString bus = QString::number(usbdevs[j].bus).rightJustified(3, '0');
      QString address = QString::number(usbdevs[j].address).rightJustified(3, '0');
      QString vid = QString::number(usbdevs[j].vid, 16);
      QString pid = QString::number(usbdevs[j].pid, 16); 
      QString s = QString("%1:%2    0x%3:0x%4").arg(bus, address, vid, pid);
      items << s;
    }

    QInputDialog *inputDialog = new QInputDialog();
    inputDialog->setOption(QInputDialog::UseListViewForComboBoxItems);
    inputDialog->setComboBoxItems(items);
    inputDialog->setWindowTitle("Available Bases");
    inputDialog->setLabelText("Please select a base");
    if (!inputDialog->exec())
      QTimer::singleShot(0, this, SLOT(close()));;
    QString item = inputDialog->textValue();
    if (!item.isEmpty())
    {
      index = items.indexOf(item);
    }
    if (index > usbdevs.size())
      index = 0;

    usbdev = usbdevs[index];
  }

  if (tag.Attach(usbdev))
  {
    Status status;
    //Info info;
    tag.GetTagInfo(info);

    tag.GetConfig(config);
    tag.GetStatus(status);

    if (!status.debug_message().empty()){
      qDebug() << "Log: " << QString::fromStdString(status.debug_message());
    }


    if (status.state() != IDLE) 
    {
        qInfo() << "Tag not in idle state";
        Detach();
        return false;
    }

    // setup UI

    ui.attachButton->setEnabled(false);
    ui.detachButton->setEnabled(true);
    ui.streamCheckBox->setEnabled(true);
    ui.streamCheckBox->setChecked(false);
    ui.saveButton->setEnabled(true);
    ui.loadButton->setEnabled(true);

    magnetic.clear();
    ui.graphWidget->reset();
    //ui.graphWidget->drawSphere(50.0);
    on_loadButton_clicked();
    qInfo() << "Attach succeeded";
    return true;
  }
  qInfo() << "Attach failed";
  return false;
}

// Detach from tag

void MainWindow::Detach()
{
  if (captureActive) {
    finishSampleCapture();
  }
  timer.stop();
  qualitytimer.stop();
  if (!replayEnabled) {
    tag.Detach();
  }
  ui.attachButton->setEnabled(true);
  ui.detachButton->setEnabled(false);
  ui.streamCheckBox->setChecked(false);
  ui.streamCheckBox->setEnabled(false);
  ui.saveButton->setEnabled(false);
  ui.loadButton->setEnabled(false);
  resetCalibrationDisplay();
  isCalibrating = false;
  isOrienting = false;
  isStreaming = false;
}

void MainWindow::on_streamCheckBox_toggled(bool checked){
  qInfo() << "Stream checkbox " << checked;
  if(checked){
    if (!replayEnabled) {
      tag.SetRtc();
      tag.Calibrate();
    }
    timer.start(100);
    isStreaming = true;
    ui.startButton->setEnabled(true);
    ui.stopButton->setEnabled(false);
    ui.clearButton->setEnabled(true);
    ui.saveButton->setEnabled(false);
    ui.loadButton->setEnabled(false);
  } else {
    if (isCalibrating || captureActive) {
      // Treat unchecking Stream the same as Stop so a maintainer does not lose
      // a useful fixture capture by ending the stream from the lower control.
      isCalibrating = false;
      finishSampleCapture();
    }
    if (!replayEnabled) {
      tag.Stop();
    }
    timer.stop();
    qualitytimer.stop();
    isStreaming = false;
    ui.startButton->setEnabled(false);
    ui.stopButton->setEnabled(false);
    ui.clearButton->setEnabled(false);
    ui.saveButton->setEnabled(true);
    ui.loadButton->setEnabled(true);
  }
}

void MainWindow::on_attachButton_clicked(){
  Attach();
}

void MainWindow::on_detachButton_clicked(){
  Detach();
}

// While tag is attached and streaming is enabled, this
// method is called at regular intervals

void MainWindow::TriggerUpdate(void)
{
  Ack ack;
  QVector3D mag, accel;
  
  if (isStreaming)
  { 
    if (replayEnabled) {
      // Fake-tag replay feeds one archived sample per timer tick, preserving
      // the same UI pacing and state transitions as a live calibration stream.
      bool hasAccel = false;
      int batchIndex = 0;
      int sampleIndex = 0;
      if (nextReplaySample(mag, hasAccel, accel, batchIndex, sampleIndex)) {
        processCalibrationSample(mag, hasAccel, accel, batchIndex, sampleIndex);
      } else {
        ui.streamCheckBox->setChecked(false);
      }
      return;
    }

    tag.GetCalibrationLog(ack);
    if (ack.has_calibration_log()) {
        const int batchIndex = captureBatchIndex++;
        int sampleIndex = 0;
        for(auto const &sdata : ack.calibration_log().data())
        {
          const int sampleIndexInBatch = sampleIndex++;
          if (sdata.has_mag()){
              mag = QVector3D(
                  sdata.mag().mx(),
                  sdata.mag().my(),
                  sdata.mag().mz());
          } else {
            continue;
          }

          if (sdata.has_accel()){
            accel = QVector3D(
                sdata.accel().ax(),
                sdata.accel().ay(),
                sdata.accel().az());
          }

          processCalibrationSample(mag, sdata.has_accel(), accel, batchIndex,
                                   sampleIndexInBatch);
        }
    }   
  } 
}

void MainWindow::processCalibrationSample(const QVector3D &mag, bool hasAccel,
                                          const QVector3D &accel,
                                          int batchIndex, int sampleIndex)
{
  if (hasAccel) {
    QQuaternion q;
    float dip;
    float field;
    if (magnetic.eCompass(mag, accel, q, dip, field)) {
      // display angles -- note that QT getEulerAngles uses a strange convention
      // pitch around x axis, yaw around y axis, and roll around z axis.  Order is YXZ for Q->angles, ZXY for angles->Q
      // we want x:pitch,y:roll,z:yaw
      // note that pitch and roll both have signs reversed.

      // Orientation is ENU
      QVector3D angles = q.toEulerAngles();
      if (angles[2] < 0.0) angles[2] += 360.0;
      const float pitch = angles[0];
      const float roll = angles[1];
      const float yaw = angles[2];
      log_trace("Pitch: %.2f Roll %.2f Yaw %.2f", pitch, roll, yaw);
      setOrientation(yaw,pitch,roll,dip,field,accel.length());
      // update rotated image of tag
      // qtquick -- roll around z, pitch around x, yaw around y
      //. this is really strange
      //QQuaternion Qprime = QQuaternion::fromEulerAngles(-pitch,yaw,roll);
      //https://stackoverflow.com/questions/28673777/convert-quaternion-from-right-handed-to-left-handed-coordinate-system
      //QQuaternion Qprime(q.scalar(),q.x(),q.z(),-q.y());

      // convert from ENU to NED

      QQuaternion qx = QQuaternion::fromAxisAndAngle(QVector3D(1,0,0),pitch);
      QQuaternion qy = QQuaternion::fromAxisAndAngle(QVector3D(0,1,0),roll);
      QQuaternion qz = QQuaternion::fromAxisAndAngle(QVector3D(0,0,1),yaw);
      rotateImage(qz*qy*qx);
    }
  }

  if (isCalibrating) {
    QVector3D plottedMag = mag;
    recordCaptureSample(batchIndex, sampleIndex, plottedMag, hasAccel, accel);
    magnetic.addData(plottedMag);
    ui.graphWidget->addPoint(plottedMag);
  }
}

bool MainWindow::nextReplaySample(QVector3D &mag, bool &hasAccel,
                                  QVector3D &accel, int &batchIndex,
                                  int &sampleIndex)
{
  if (!replayEnabled || replayCursor >= replaySamples.size()) {
    return false;
  }

  const ReplaySample &sample = replaySamples.at(replayCursor);
  mag = sample.mag;
  hasAccel = sample.hasAccel;
  accel = sample.accel;
  batchIndex = replayCursor;
  sampleIndex = 0;
  replayCursor++;
  return true;
}

void MainWindow::prepareReplayMilestone(int percent)
{
  if (!replayEnabled || replaySamples.isEmpty()) {
    return;
  }

  // Milestone screenshots deliberately run the production calibration path.
  // They do not restore saved calibration constants from the fixture; that
  // would document a different code path than the one users exercise.
  percent = qBound(0, percent, 100);
  resetReplayCollection();
  replayCursor = 0;
  ui.tabWidget->setCurrentWidget(ui.calTab);
  ui.streamCheckBox->setChecked(true);

  if (!isCalibrating) {
    on_startButton_clicked();
  }

  const int sampleLimit =
      static_cast<int>((static_cast<qint64>(replaySamples.size()) * percent) / 100);
  for (int i = 0; i < sampleLimit; i++) {
    QVector3D mag;
    QVector3D accel;
    bool hasAccel = false;
    int batchIndex = 0;
    int sampleIndex = 0;
    if (!nextReplaySample(mag, hasAccel, accel, batchIndex, sampleIndex)) {
      break;
    }
    processCalibrationSample(mag, hasAccel, accel, batchIndex, sampleIndex);
  }

  if (sampleLimit > 0) {
    magnetic.qualityUpdate();
    calibration_update();
  }

  if (percent >= 100) {
    on_stopButton_clicked();
    ui.streamCheckBox->setChecked(false);
  } else {
    timer.stop();
  }

  qInfo() << "Prepared replay milestone" << percent << "percent with"
          << sampleLimit << "samples";
}

void MainWindow::prepareReplayOrientation(bool batteryForwardConvention)
{
  if (!replayEnabled || replaySamples.isEmpty()) {
    return;
  }

  // The orientation tab needs a valid calibration first. Replaying the full
  // collection produces those parameters, then the screenshot pose below
  // replaces the capture's arbitrary last attitude with a deterministic one.
  prepareReplayMilestone(100);
  timer.stop();
  qualitytimer.stop();
  qApp->processEvents();

  ui.tabWidget->setCurrentWidget(ui.orientationTab);
  {
    QSignalBlocker streamBlocker(ui.streamCheckBox);
    ui.streamCheckBox->setChecked(true);
  }
  isStreaming = true;
  applyDocumentationOrientationPose(batteryForwardConvention);
}

void MainWindow::applyDocumentationOrientationPose(bool batteryForwardConvention)
{
  if (batteryForwardAction) {
    batteryForwardAction->setChecked(batteryForwardConvention);
  }
  // Call the slot directly after changing the action because screenshot
  // automation can run before a queued menu/action update has repainted QML.
  batteryForwardToggled(batteryForwardConvention);
  qApp->processEvents();

  QList<float> pose = orientationPose;
  if (pose.size() != 6) {
    // Chosen to show heading, pitch, roll, dip, field, and gravity values that
    // visibly change under the battery-backward convention while remaining
    // easy to read in the documentation screenshots.
    pose = {45.0f, 12.0f, -18.0f, 62.0f, 46.5f, 1000.0f};
  }

  const float heading = pose.at(0);
  const float pitch = pose.at(1);
  const float roll = pose.at(2);
  const float dip = pose.at(3);
  const float field = pose.at(4);
  const float gravity = pose.at(5);
  setOrientation(heading, pitch, roll, dip, field, gravity);

  const QQuaternion qx = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), pitch);
  const QQuaternion qy = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), roll);
  const QQuaternion qz = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), heading);
  rotateImage(qz * qy * qx);

  qInfo() << "Prepared replay orientation pose" << heading << pitch << roll
          << "battery forward" << batteryForwardConvention;
}

void MainWindow::resetReplayCollection()
{
  timer.stop();
  qualitytimer.stop();
  replayCursor = 0;
  isCalibrating = false;
  isOrienting = false;
  isStreaming = false;

  {
    QSignalBlocker streamBlocker(ui.streamCheckBox);
    ui.streamCheckBox->setChecked(false);
  }

  ui.streamCheckBox->setEnabled(true);
  ui.startButton->setEnabled(false);
  ui.stopButton->setEnabled(false);
  ui.clearButton->setEnabled(false);
  ui.saveButton->setEnabled(false);
  ui.loadButton->setEnabled(false);
  ui.actionSave_Sample_Capture->setEnabled(false);

  ui.graphWidget->reset();
  magnetic.clear();
  resetCalibrationDisplay();
  clearSampleCapture();
}

void MainWindow::resetCalibrationDisplay()
{
  ui.bLabel->setText("--");
  ui.a0Label->setText("-- -- --");
  ui.a1Label->setText("-- -- --");
  ui.a2Label->setText("-- -- --");
  ui.v0Label->setText("--");
  ui.v1Label->setText("--");
  ui.v2Label->setText("--");
  ui.qualityLabel->setText("--      --       --       --");
}

bool MainWindow::saveCurrentScreenshot(const QString &path)
{
  resize(1040, 900);
  showNormal();
  raise();
  activateWindow();
  qApp->processEvents();
  repaint();
  qApp->processEvents();

  QPixmap pixmap;
  if (QScreen *screen = windowHandle() ? windowHandle()->screen() : nullptr) {
    // Widget grabs omit the native title/menu area and were clipped on macOS.
    // Grab the screen rectangle for the window frame first, then fall back to
    // narrower captures for headless or non-composited environments.
    const QRect frame = frameGeometry();
    pixmap = screen->grabWindow(0, frame.x(), frame.y(), frame.width(),
                                frame.height());
    if (pixmap.isNull()) {
      pixmap = screen->grabWindow(winId());
    }
  }
  if (pixmap.isNull()) {
    pixmap = grab();
  }
  if (pixmap.isNull()) {
    qWarning() << "Unable to capture screenshot" << path;
    return false;
  }

  if (!pixmap.save(path, "PNG")) {
    qWarning() << "Unable to save screenshot" << path;
    return false;
  }

  qInfo() << "Saved screenshot" << path;
  return true;
}

void MainWindow::captureStartupScreenshot()
{
  bool ok = true;
  QDir dir(screenshotDir);
  if (!dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  if (ok) {
    ui.tabWidget->setCurrentWidget(ui.calTab);
    ok = saveCurrentScreenshot(dir.filePath("qtcalibrate-startup.png"));
  }

  qApp->exit(ok ? 0 : 1);
}

void MainWindow::captureOrientationScreenshot()
{
  bool ok = true;
  if (!replayEnabled) {
    qWarning() << "Orientation screenshot requires --replay-capture";
    ok = false;
  }

  QDir dir(screenshotDir);
  if (ok && !dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  if (ok) {
    prepareReplayOrientation(true);
    qApp->processEvents();
    applyDocumentationOrientationPose(true);
    qApp->processEvents();
    // Apply the pose again after a repaint delay so the second screenshot in
    // this sequence does not inherit the first screenshot's QML convention.
    waitForGuiToSettle(300);
    applyDocumentationOrientationPose(true);
    waitForGuiToSettle(300);
    ok = saveCurrentScreenshot(dir.filePath("qtcalibrate-orientation-forward.png"));
  }

  if (ok) {
    prepareReplayOrientation(false);
    qApp->processEvents();
    applyDocumentationOrientationPose(false);
    qApp->processEvents();
    // See the forward path above. The repeated application is intentionally
    // symmetrical because battery-forward/backward changes propagate through
    // both QWidget labels and Qt Quick properties.
    waitForGuiToSettle(300);
    applyDocumentationOrientationPose(false);
    waitForGuiToSettle(300);
    ok = saveCurrentScreenshot(dir.filePath("qtcalibrate-orientation-backward.png"));
  }

  qApp->exit(ok ? 0 : 1);
}

void MainWindow::captureReplayScreenshots()
{
  bool ok = true;
  if (!replayEnabled) {
    qWarning() << "Replay screenshots require --replay-capture";
    ok = false;
  }

  QDir dir(screenshotDir);
  if (ok && !dir.exists() && !dir.mkpath(".")) {
    qWarning() << "Unable to create screenshot directory" << screenshotDir;
    ok = false;
  }

  const int milestones[] = {0, 25, 50, 100};
  for (int percent : milestones) {
    if (!ok) {
      break;
    }
    prepareReplayMilestone(percent);
    const QString filename =
        QString("%1-%2.png")
            .arg(screenshotPrefix)
            .arg(percent, 3, 10, QLatin1Char('0'));
    ok = saveCurrentScreenshot(dir.filePath(filename));
  }

  if (ok) {
    qInfo() << "Replay screenshots written to" << dir.absolutePath();
  }
  qApp->exit(ok ? 0 : 1);
}

// called from compassdata (magnetic) when data is added

void MainWindow::calibration_update(void)
{
  float B;
  float V[3];
  float A[3][3];
  //qInfo() << "Calibration update in mainwindow";

  if (!magnetic.getCalibrationConstants(&B, V, A)) {
    resetCalibrationDisplay();
    return;
  }

  ui.bLabel->setText(QString::asprintf("%.2f",B));

  ui.a0Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[0][0],A[0][1],A[0][2]));
  ui.a1Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[1][0],A[1][1],A[1][2]));
  ui.a2Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[2][0],A[2][1],A[2][2]));

  ui.v0Label->setText(QString::asprintf("%+.3f",V[0]));
  ui.v1Label->setText(QString::asprintf("%+.3f",V[1]));
  ui.v2Label->setText(QString::asprintf("%+.3f",V[2]));
  
  float gaps, variance, wobble, fiterror;
  magnetic.calibrationQuality(gaps, variance, wobble, fiterror);
  ui.qualityLabel->setText(QString::asprintf("%2.1f%%   %2.1f%%      %2.1f%%      %2.1f%%",gaps,variance,wobble,fiterror));

  //ui.graphWidget->setData(data);
  QList<QVector3D> points;
  //for (QScatterDataItem item : data){
  //  points.append(item.position());
  //}
  magnetic.getData(points);
  ui.graphWidget->setPoints(points);
  ui.graphWidget->setField(magnetic.getField());
}

void MainWindow::TriggerQualityUpdate()
{
  magnetic.qualityUpdate(); 
}


//  Calibration data collection

void MainWindow::on_clearButton_clicked()
{
  //qInfo() << "clear clicked";
  ui.graphWidget->reset();
  magnetic.clear();
  resetCalibrationDisplay();
  clearSampleCapture();
}

void MainWindow::on_startButton_clicked(){
  if (isStreaming) {
    // Captures start with the user's calibration collection, not merely with
    // Stream, so the saved fixture matches the visible scatter plot sequence.
    beginSampleCapture();
    isCalibrating = true;
  //qInfo() << "connect clicked";
    ui.graphWidget->setFocusQuaternion(QQuaternion(1.0,0.0,0.0,0.0));
    //QScatterDataArray data;
    //magnetic.getRegionData(data,50.0);
    //ui.graphWidget->setRegionData(data);
    ui.startButton->setEnabled(false);
    ui.stopButton->setEnabled(true);
    ui.clearButton->setEnabled(false);
    qualitytimer.start(200);
  }
}

  
void MainWindow::on_stopButton_clicked(){
  isCalibrating = false;
  qualitytimer.stop();
  finishSampleCapture();
  ui.startButton->setEnabled(true);
  ui.stopButton->setEnabled(false);
  ui.clearButton->setEnabled(true);
}

void MainWindow::beginSampleCapture()
{
  captureSamples.clear();
  captureStartedUtc = QDateTime::currentDateTimeUtc();
  captureStoppedUtc = QDateTime();
  captureBatchIndex = 0;
  captureActive = true;
  captureReady = false;
  ui.actionSave_Sample_Capture->setEnabled(false);
  qInfo() << "Calibration sample capture started";
}

void MainWindow::recordCaptureSample(int batchIndex, int sampleIndex,
                                     const QVector3D &mag, bool hasAccel,
                                     const QVector3D &accel)
{
  if (!captureActive) {
    return;
  }

  CalibrationCaptureSample sample;
  sample.batchIndex = batchIndex;
  sample.sampleIndex = sampleIndex;
  sample.mag = mag;
  sample.hasAccel = hasAccel;
  sample.accel = accel;
  captureSamples.append(sample);
}

void MainWindow::finishSampleCapture()
{
  if (!captureActive) {
    return;
  }

  captureStoppedUtc = QDateTime::currentDateTimeUtc();
  captureActive = false;
  captureReady = !captureSamples.isEmpty();
  ui.actionSave_Sample_Capture->setEnabled(captureReady);

  if (captureReady) {
    qInfo() << "Calibration sample capture stopped with"
            << captureSamples.size() << "samples";
  } else {
    qInfo() << "Calibration sample capture stopped with no samples";
  }
}

void MainWindow::clearSampleCapture()
{
  captureSamples.clear();
  captureStartedUtc = QDateTime();
  captureStoppedUtc = QDateTime();
  captureBatchIndex = 0;
  captureActive = false;
  captureReady = false;
  ui.actionSave_Sample_Capture->setEnabled(false);
}

bool MainWindow::saveSampleCapture(const QString &path)
{
  if (!captureReady || captureSamples.isEmpty()) {
    qInfo() << "No calibration sample capture is available to save";
    return false;
  }

  // Keep this schema intentionally close to the replay reader above: raw
  // samples are the machine contract, while calibration values and quality
  // metrics are stored so maintainers can judge whether a fixture is useful.
  QJsonObject root;
  root["schema"] = "tag-designs.qtcalibrate.calibration-capture.v1";
  root["application"] = "qtcalibrate";
  root["created_at_utc"] =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  root["capture_started_utc"] = captureStartedUtc.toString(Qt::ISODateWithMs);
  root["capture_stopped_utc"] = captureStoppedUtc.toString(Qt::ISODateWithMs);
  root["sample_count"] = captureSamples.size();

  QJsonObject units;
  units["mag"] = "calibration stream magnetometer units";
  units["accel"] = "calibration stream accelerometer units";
  root["units"] = units;

  QJsonArray samples;
  for (int i = 0; i < captureSamples.size(); i++) {
    const CalibrationCaptureSample &sample = captureSamples.at(i);
    QJsonObject sampleObject;
    sampleObject["sequence"] = i;
    sampleObject["batch_index"] = sample.batchIndex;
    sampleObject["sample_index"] = sample.sampleIndex;
    sampleObject["mag"] = magToJson(sample.mag);
    sampleObject["has_accel"] = sample.hasAccel;
    if (sample.hasAccel) {
      sampleObject["accel"] = accelToJson(sample.accel);
    }
    samples.append(sampleObject);
  }
  root["samples"] = samples;

  QJsonObject calibration;
  float B;
  float V[3];
  float A[3][3];
  const bool hasCalibration = magnetic.getCalibrationConstants(&B, V, A);
  calibration["valid"] = hasCalibration;
  if (hasCalibration) {
    calibration["field"] = B;
    QJsonArray offset;
    for (int i = 0; i < 3; i++) {
      offset.append(V[i]);
    }
    calibration["offset"] = offset;

    QJsonArray mapping;
    for (int row = 0; row < 3; row++) {
      QJsonArray rowValues;
      for (int column = 0; column < 3; column++) {
        rowValues.append(A[row][column]);
      }
      mapping.append(rowValues);
    }
    calibration["mapping"] = mapping;

    magnetic.qualityUpdate();
    float gaps;
    float variance;
    float wobble;
    float fiterror;
    magnetic.calibrationQuality(gaps, variance, wobble, fiterror);
    QJsonObject quality;
    quality["surface_gap_error_percent"] = gaps;
    quality["magnitude_variance_error_percent"] = variance;
    quality["wobble_error_percent"] = wobble;
    quality["spherical_fit_error_percent"] = fiterror;
    calibration["quality"] = quality;
  }
  root["calibration"] = calibration;

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Cannot open sample capture file for writing"));
    return false;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    QMessageBox::critical(this, tr("Error"),
                          tr("Cannot save sample capture file"));
    return false;
  }

  qInfo() << "Saved calibration sample capture" << path;
  return true;
}

void MainWindow::on_actionSave_Sample_Capture_triggered()
{
  if (!captureReady || captureSamples.isEmpty()) {
    QMessageBox::information(this, tr("Save Sample Capture"),
                             tr("No stopped calibration sample capture is available."));
    return;
  }

  const QString timestamp =
      captureStoppedUtc.isValid()
          ? captureStoppedUtc.toString("yyyyMMdd-HHmmss")
          : QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
  QString nameFile = HostFileDialog::getSaveFileName(
      this, tr("Save Sample Capture"),
      QDir::homePath() + QString("/qtcalibrate-samples-%1.json").arg(timestamp),
      tr("JSON Files (*.json);;All Files (*)"));
  if (nameFile.isEmpty()) {
    return;
  }

  if (QFileInfo(nameFile).suffix().isEmpty()) {
    nameFile += ".json";
  }
  saveSampleCapture(nameFile);
}

// save/restore calibration

void MainWindow::on_saveButton_clicked(){
  CalibrationConstants constants;
  CalibrationConstants_MagConstants magconstants;

  float B;
  float V[3];
  float A[3][3];
  if (magnetic.getCalibrationConstants(&B, V, A)){
    magconstants.set_b(B);
    magconstants.set_v0(V[0]);
    magconstants.set_v1(V[1]);
    magconstants.set_v2(V[2]);
    magconstants.set_a00(A[0][0]);
    magconstants.set_a01(A[0][1]);
    magconstants.set_a02(A[0][2]);
    magconstants.set_a10(A[1][0]);
    magconstants.set_a11(A[1][1]);
    magconstants.set_a12(A[1][2]);
    magconstants.set_a20(A[2][0]);
    magconstants.set_a21(A[2][1]);
    magconstants.set_a22(A[2][2]);
    constants.set_allocated_magnetometer(new ::CalibrationConstants_MagConstants(magconstants));
    constants.set_timestamp(QDateTime::currentSecsSinceEpoch());
    if (!tag.WriteCalibration(constants))
    {
      qInfo() << "WriteCalibration failed";
    }
    //qInfo() << magconstants.DebugString();

  } else {
    qInfo() << "save failed";
  }

}

void MainWindow::on_loadButton_clicked(){
   CalibrationConstants constants;
   float B;
   float V[3];
   float A[3][3];

   if (tag.ReadCalibration(constants,-1)
       // && ack.has_calibration_constants() 
        && constants.has_magnetometer())
   {
      const CalibrationConstants_MagConstants mag = constants.magnetometer();
      B = mag.b();
      V[0] = mag.v0();
      V[1] = mag.v1();
      V[2] = mag.v2();
      A[0][0] = mag.a00();
      A[0][1] = mag.a01();
      A[0][2] = mag.a02();
      A[1][0] = mag.a10();
      A[1][1] = mag.a11();
      A[1][2] = mag.a12();
      A[2][0] = mag.a20();
      A[2][1] = mag.a21();
      A[2][2] = mag.a22();
      magnetic.setCalibrationConstants(B,V,A);

      ui.bLabel->setText(QString::asprintf("%.2f",B));

      ui.a0Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[0][0],A[0][1],A[0][2]));
      ui.a1Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[1][0],A[1][1],A[1][2]));
      ui.a2Label->setText(QString::asprintf("%+.3f %+.3f %+.3f", A[2][0],A[2][1],A[2][2]));

      ui.v0Label->setText(QString::asprintf("%+.3f",V[0]));
      ui.v1Label->setText(QString::asprintf("%+.3f",V[1]));
      ui.v2Label->setText(QString::asprintf("%+.3f",V[2]));

      qInfo() << "Read timestamp " << constants.timestamp();
     
   } else {
      qInfo() << "Read calibration failed";
   }



}

// Logging of error messages

void MainWindow::logWindowInit(void)
{

    QStringList ll;

    // don't include LOG_FATAL in choices

    for (int i = 0; i < LOG_FATAL; i++) {
        ll << log_level_string(i);
    }

    ui.loglevelBox->addItems(ll);
    ui.loglevelBox->setCurrentIndex(LOG_INFO);

    // connect log text edit box to error logging system

    s_textEdit = ui.logTextEdit;
    qInstallMessageHandler(myMessageOutput);

}

void MainWindow::on_loglevelBox_currentIndexChanged(int index)
{
  log_set_level(index);
  log_level = index;
}

// Save the log window contents as a text file

void MainWindow::on_logsaveButton_clicked()
{
  QString nameFile = HostFileDialog::getSaveFileName(
      this, tr("Save Log"), QDir::homePath());
  if (nameFile != "")
  {
    QFile file(nameFile);

    if (file.open(QIODevice::ReadWrite))
    {
      QTextStream stream(&file);
      stream << ui.logTextEdit->toPlainText();
      file.flush();
      file.close();
    }
    else
    {
      QMessageBox::critical(this, tr("Error"), tr("Cannot open file"));
      return;
    }
  }
}

void MainWindow::on_logclearButton_clicked()
{
  ui.logTextEdit->clear();
}

void MainWindow::orientationControlsInit()
{
  ui.menuBar->setNativeMenuBar(false);

  configurationMenu = ui.menuBar->addMenu(tr("&Configuration"));

  declinationAction = new QAction(this);
  updateDeclinationActionText();
  connect(declinationAction, &QAction::triggered, this, &MainWindow::setDeclination);
  configurationMenu->addAction(declinationAction);

  batteryForwardAction = new QAction(tr("&Battery Forward"), this);
  batteryForwardAction->setCheckable(true);
  batteryForwardAction->setChecked(batteryForward);
  connect(
      batteryForwardAction,
      &QAction::toggled,
      this,
      &MainWindow::batteryForwardToggled);
  configurationMenu->addAction(batteryForwardAction);

  ui.tagWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  ui.attitudeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(
      ui.tagWidget,
      &QWidget::customContextMenuRequested,
      this,
      &MainWindow::showOrientationContextMenu);
  connect(
      ui.attitudeWidget,
      &QWidget::customContextMenuRequested,
      this,
      &MainWindow::showOrientationContextMenu);
}

void MainWindow::updateDeclinationActionText()
{
  if (!declinationAction) {
    return;
  }

  declinationAction->setText(
      tr("&Declination... (%1 deg)").arg(declinationDegrees, 0, 'f', 2));
}

void MainWindow::setDeclination()
{
  bool ok = false;
  const double declination = QInputDialog::getDouble(
      this,
      tr("Declination"),
      tr("Declination angle (degrees)"),
      declinationDegrees,
      -180.0,
      180.0,
      2,
      &ok);
  if (!ok) {
    return;
  }

  declinationDegrees = declination;
  updateDeclinationActionText();
  compassDisplay.setDeclination(declinationDegrees);
}

void MainWindow::batteryForwardToggled(bool checked)
{
  batteryForward = checked;
  compassDisplay.setBatteryForward(batteryForward);
  attitudeDisplay.setBatteryForward(batteryForward);
}

void MainWindow::showOrientationContextMenu(const QPoint &pos)
{
  QWidget *widget = qobject_cast<QWidget *>(sender());
  if (!widget) {
    return;
  }

  QMenu menu(this);
  menu.addAction(declinationAction);
  menu.addAction(batteryForwardAction);
  menu.exec(widget->mapToGlobal(pos));
}

void MainWindow::rotateImage(QQuaternion qt){
    attitudeDisplay.setRotationQuaternion(qt);
}

void MainWindow::setOrientation(float h, float p, float r, float d, float f, float g){
  CompassDerivedSample sample;
  sample.yaw = 360 - h;
  sample.pitch = p;
  sample.roll = r;
  sample.dip = d;
  sample.field = f;
  sample.mg = g;
  compassDisplay.showSample(sample);
}

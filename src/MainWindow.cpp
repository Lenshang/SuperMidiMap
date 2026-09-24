#include "MainWindow.h"
#include "MidiClip.h"
#include "MidiEngine.h"
#include "PadGridWidget.h"
#include "NoteNames.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// 按优先级自动挑选设备：输入优先找打击垫，输出优先找 Loopback (A)
int pickDevice(const QStringList &list, std::initializer_list<const char *> prefs)
{
    for (const char *p : prefs) {
        for (int i = 0; i < list.size(); ++i) {
            if (list[i].contains(QLatin1String(p), Qt::CaseInsensitive))
                return i;
        }
    }
    return list.isEmpty() ? -1 : 0;
}

// 音符下拉框支持输入过滤：键入音名或号码（如 c4 / 60）实时过滤，回车提交
void makeNoteComboFilterable(QComboBox *combo)
{
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setMaxVisibleItems(16);

    QCompleter *completer = new QCompleter(combo->model(), combo);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    combo->setCompleter(completer);

    auto commitText = [combo] {
        QLineEdit *le = combo->lineEdit();
        const QString text = le->text().trimmed();
        if (text.isEmpty())
            return;
        // 1) 完整文本精确匹配（不区分大小写）
        int i = combo->findText(text, Qt::MatchFixedString);
        // 2) 纯数字直接当作音符号
        bool numOk = false;
        const int num = text.toInt(&numOk);
        if (i < 0 && numOk && num >= 0 && num <= 127)
            i = num;
        // 3) 补全弹窗当前高亮的候选
        if (i < 0) {
            if (QCompleter *c = combo->completer(); c && c->completionCount() > 0)
                i = combo->findText(c->currentCompletion(), Qt::MatchFixedString);
        }
        // 4) 包含匹配的第一个结果
        if (i < 0) {
            const QString lower = text.toLower();
            for (int k = 0; k < combo->count(); ++k) {
                if (combo->itemText(k).toLower().contains(lower)) {
                    i = k;
                    break;
                }
            }
        }
        if (i >= 0)
            combo->setCurrentIndex(i);  // 提交后文本自动规范化为完整项文本
    };
    QObject::connect(combo->lineEdit(), &QLineEdit::returnPressed, combo, commitText);
    QObject::connect(combo->lineEdit(), &QLineEdit::editingFinished, combo, commitText);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_engine = new MidiEngine(this);
    buildUi();

    connect(m_engine, &MidiEngine::padActivity, m_grid, &PadGridWidget::onPadActivity);
    connect(m_engine, &MidiEngine::stateChanged, this, &MainWindow::updateStatus);
    connect(m_engine, &MidiEngine::errorOccurred, this, &MainWindow::onEngineError);

    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statusTimer->start(500);

    m_deviceTimer = new QTimer(this);
    connect(m_deviceTimer, &QTimer::timeout, this, [this] { refreshDevices(false); });
    m_deviceTimer->start(2000);  // 轮询设备列表，实现热插拔检测与自动重连

    setWindowTitle(tr("SuperMidiMap — MIDI 打击垫映射器"));
    resize(1020, 660);

    restoreSettings();
    applyProfileToEngine();
    refreshDevices(true);
}

void MainWindow::buildUi()
{
    // ---- 菜单 ----
    QMenu *profileMenu = menuBar()->addMenu(tr("配置档(&F)"));
    profileMenu->addAction(tr("保存配置档..."), this, &MainWindow::onSaveProfile, QKeySequence::Save);
    profileMenu->addAction(tr("加载配置档..."), this, &MainWindow::onLoadProfile, QKeySequence::Open);
    profileMenu->addSeparator();
    profileMenu->addAction(tr("恢复默认映射"), this, &MainWindow::onResetDefaults);

    // ---- 中央布局 ----
    auto *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(14, 14, 14, 8);
    root->setSpacing(14);

    auto *left = new QVBoxLayout;

    auto *deviceGroup = new QGroupBox(tr("MIDI 设备"), central);
    auto *deviceForm = new QFormLayout(deviceGroup);
    deviceForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_inputBox = new QComboBox(deviceGroup);
    m_outputBox = new QComboBox(deviceGroup);
    deviceForm->addRow(tr("输入（打击垫）"), m_inputBox);
    deviceForm->addRow(tr("输出（Loopback OUT）"), m_outputBox);
    m_deviceHint = new QLabel(deviceGroup);
    m_deviceHint->setWordWrap(true);
    m_deviceHint->setStyleSheet(QStringLiteral("color:#8a93a6; font-size:12px; background:transparent;"));
    m_deviceHint->hide();
    deviceForm->addRow(QString(), m_deviceHint);
    auto *deviceButtons = new QHBoxLayout;
    auto *refreshButton = new QPushButton(tr("刷新设备"), deviceGroup);
    auto *testButton = new QPushButton(tr("测试输出"), deviceGroup);
    deviceButtons->addWidget(refreshButton);
    deviceButtons->addWidget(testButton);
    deviceButtons->addStretch(1);
    deviceForm->addRow(QString(), deviceButtons);
    left->addWidget(deviceGroup);

    m_grid = new PadGridWidget(central);
    left->addWidget(m_grid, 1);
    root->addLayout(left, 3);

    auto *right = new QVBoxLayout;

    auto *padGroup = new QGroupBox(tr("打击垫映射"), central);
    auto *padForm = new QFormLayout(padGroup);
    padForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_padTitleLabel = new QLabel(padGroup);
    padForm->addRow(m_padTitleLabel);

    m_modeBox = new QComboBox(padGroup);
    m_modeBox->addItem(tr("音符（重映射）"), 0);
    m_modeBox->addItem(tr("MIDI 片段（按住播放）"), 1);
    padForm->addRow(tr("模式"), m_modeBox);

    m_sourceBox = new QComboBox(padGroup);
    for (int i = 0; i < 128; ++i)
        m_sourceBox->addItem(NoteNames::withNumber(i), i);
    makeNoteComboFilterable(m_sourceBox);
    m_sourceBox->lineEdit()->setPlaceholderText(tr("输入音名或号码，如 C4 / 60"));
    padForm->addRow(tr("源音符（设备发送）"), m_sourceBox);

    m_noteBox = new QComboBox(padGroup);
    for (int i = 0; i < 128; ++i)
        m_noteBox->addItem(NoteNames::withNumber(i), i);
    makeNoteComboFilterable(m_noteBox);
    m_noteBox->lineEdit()->setPlaceholderText(tr("输入音名或号码，如 C4 / 60"));
    padForm->addRow(tr("目标音符"), m_noteBox);

    auto *clipRow = new QWidget(padGroup);
    auto *clipLayout = new QHBoxLayout(clipRow);
    clipLayout->setContentsMargins(0, 0, 0, 0);
    clipLayout->setSpacing(6);
    m_clipPathEdit = new QLineEdit(clipRow);
    m_clipPathEdit->setReadOnly(true);
    m_clipPathEdit->setPlaceholderText(tr("未选择 MIDI 文件"));
    m_browseClipButton = new QPushButton(tr("浏览..."), clipRow);
    clipLayout->addWidget(m_clipPathEdit, 1);
    clipLayout->addWidget(m_browseClipButton);
    padForm->addRow(tr("MIDI 文件"), clipRow);

    auto *clipPlayRow = new QWidget(padGroup);
    auto *playLayout = new QHBoxLayout(clipPlayRow);
    playLayout->setContentsMargins(0, 0, 0, 0);
    playLayout->setSpacing(6);
    m_bpmBox = new QSpinBox(clipPlayRow);
    m_bpmBox->setRange(30, 300);
    m_bpmBox->setValue(120);
    m_bpmBox->setSuffix(tr(" BPM"));
    m_loopBox = new QCheckBox(tr("按住期间循环"), clipPlayRow);
    m_loopBox->setChecked(true);
    playLayout->addWidget(m_bpmBox, 1);
    playLayout->addWidget(m_loopBox);
    padForm->addRow(tr("播放"), clipPlayRow);

    m_channelBox = new QComboBox(padGroup);
    m_channelBox->addItem(tr("跟随原通道"), 0);
    for (int c = 1; c <= 16; ++c)
        m_channelBox->addItem(QString::number(c), c);
    padForm->addRow(tr("输出通道"), m_channelBox);
    m_muteBox = new QCheckBox(tr("静音此垫（不转发）"), padGroup);
    padForm->addRow(QString(), m_muteBox);
    right->addWidget(padGroup);

    auto *globalGroup = new QGroupBox(tr("全局"), central);
    auto *globalLayout = new QVBoxLayout(globalGroup);
    m_passthroughBox = new QCheckBox(tr("透传未映射的音符与其他消息"), globalGroup);
    globalLayout->addWidget(m_passthroughBox);
    auto *globalButtons = new QHBoxLayout;
    auto *panicButton = new QPushButton(tr("Panic（关闭全部音符）"), globalGroup);
    panicButton->setObjectName(QStringLiteral("panicButton"));
    globalButtons->addWidget(panicButton);
    globalButtons->addStretch(1);
    globalLayout->addLayout(globalButtons);
    right->addWidget(globalGroup);

    auto *profileGroup = new QGroupBox(tr("配置档"), central);
    auto *profileLayout = new QVBoxLayout(profileGroup);
    m_profileBox = new QComboBox(profileGroup);
    m_profileBox->setPlaceholderText(tr("（尚无已保存的配置档）"));
    m_profileBox->setToolTip(tr("选择已保存的配置档，选中立即加载"));
    profileLayout->addWidget(m_profileBox);
    auto *profileButtons = new QHBoxLayout(profileGroup);
    auto *saveButton = new QPushButton(tr("保存..."), profileGroup);
    auto *deleteButton = new QPushButton(tr("删除"), profileGroup);
    auto *resetButton = new QPushButton(tr("恢复默认"), profileGroup);
    profileButtons->addWidget(saveButton);
    profileButtons->addWidget(deleteButton);
    profileButtons->addWidget(resetButton);
    profileLayout->addLayout(profileButtons);
    right->addWidget(profileGroup);
    right->addStretch(1);
    root->addLayout(right, 2);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);

    // ---- 托盘 ----
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_tray = new QSystemTrayIcon(windowIcon(), this);
        auto *trayMenu = new QMenu(this);
        trayMenu->addAction(tr("显示 / 隐藏主窗口"), this, [this] {
            if (isVisible())
                hide();
            else {
                showNormal();
                activateWindow();
            }
        });
        trayMenu->addSeparator();
        trayMenu->addAction(tr("退出"), qApp, &QCoreApplication::quit);
        m_tray->setContextMenu(trayMenu);
        m_tray->setToolTip(tr("SuperMidiMap"));
        m_tray->show();
        // 点击托盘图标还原窗口
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger
                        || reason == QSystemTrayIcon::DoubleClick) {
                        setWindowState(windowState() & ~Qt::WindowMinimized);
                        show();
                        raise();
                        activateWindow();
                    }
                });
    }

    // ---- 信号 ----
    connect(m_grid, &PadGridWidget::padClicked, this, &MainWindow::onPadSelected);
    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshDevices(true); });
    connect(testButton, &QPushButton::clicked, this, &MainWindow::onTestOutputClicked);
    connect(m_inputBox, &QComboBox::currentIndexChanged, this, &MainWindow::onDeviceSelectionChanged);
    connect(m_outputBox, &QComboBox::currentIndexChanged, this, &MainWindow::onDeviceSelectionChanged);
    connect(m_sourceBox, &QComboBox::currentIndexChanged, this, &MainWindow::onPadEdited);
    connect(m_noteBox, &QComboBox::currentIndexChanged, this, &MainWindow::onPadEdited);
    connect(m_modeBox, &QComboBox::currentIndexChanged, this, [this] {
        if (m_updatingUi)
            return;
        updatePadEditorEnabled();
        onPadEdited();
    });
    connect(m_browseClipButton, &QPushButton::clicked, this, &MainWindow::onBrowseClip);
    connect(m_bpmBox, &QSpinBox::valueChanged, this, &MainWindow::onPadEdited);
    connect(m_loopBox, &QCheckBox::toggled, this, &MainWindow::onPadEdited);
    connect(m_channelBox, &QComboBox::currentIndexChanged, this, &MainWindow::onPadEdited);
    connect(m_muteBox, &QCheckBox::toggled, this, &MainWindow::onPadEdited);
    connect(m_passthroughBox, &QCheckBox::toggled, this, &MainWindow::onPassthroughChanged);
    connect(panicButton, &QPushButton::clicked, this, &MainWindow::onPanicClicked);
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::onSaveProfile);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::onDeleteProfile);
    connect(resetButton, &QPushButton::clicked, this, &MainWindow::onResetDefaults);
    connect(m_profileBox, &QComboBox::activated, this, &MainWindow::onProfileActivated);
    m_profileBox->installEventFilter(this);  // 点开下拉前自动刷新目录里的配置档列表
}

// ---- 设备管理 ----

void MainWindow::onDeviceSelectionChanged()
{
    if (m_updatingUi)
        return;
    if (m_inputBox->count())
        m_profile.inputDevice = m_inputBox->currentText();
    if (m_outputBox->count())
        m_profile.outputDevice = m_outputBox->currentText();
    updateDeviceHint();
    restartEngine();   // 立即用新设备重启引擎
    autosaveProfile();
    updateStatus();
}

void MainWindow::updateDeviceHint()
{
    if (!m_deviceHint)
        return;
    const QString in = m_inputBox->currentText();
    const QString out = m_outputBox->currentText();
    const bool inA = in.contains(QLatin1String("(a)"), Qt::CaseInsensitive);
    const bool inB = in.contains(QLatin1String("(b)"), Qt::CaseInsensitive);
    const bool outA = out.contains(QLatin1String("(a)"), Qt::CaseInsensitive);
    const bool outB = out.contains(QLatin1String("(b)"), Qt::CaseInsensitive);

    QString hint;
    QColor color(0x8a, 0x93, 0xa6);
    if ((inA && outB) || (inB && outA)) {
        hint = tr("⚠ 输入与输出是同一对环回的交叉两端，数据会绕回自己形成自激循环，引擎已停止。"
                  "请让输出与输入使用同名一端（A→A / B→B），或改用物理打击垫作为输入。");
        color = QColor(0xff, 0x7b, 0x72);
    } else if (outA) {
        hint = tr("微软环回为交叉环回：发往 (A) 的 OUT 会从 (B) 的 IN 出现 —— DAW 输入请选 (B)");
    } else if (outB) {
        hint = tr("微软环回为交叉环回：发往 (B) 的 OUT 会从 (A) 的 IN 出现 —— DAW 输入请选 (A)");
    }
    m_deviceHint->setStyleSheet(QStringLiteral(
        "color: %1; font-size:12px; background:transparent;").arg(color.name()));
    if (hint.isEmpty()) {
        m_deviceHint->hide();
        return;
    }
    m_deviceHint->setText(hint);
    m_deviceHint->show();
}

bool MainWindow::feedbackRisk() const
{
#ifdef Q_OS_WIN
    // Windows MIDI 服务环回对为交叉接线：输入/输出取同一对的两端会自激
    const QString in = m_inputBox->currentText();
    const QString out = m_outputBox->currentText();
    const bool inA = in.contains(QLatin1String("(a)"), Qt::CaseInsensitive);
    const bool inB = in.contains(QLatin1String("(b)"), Qt::CaseInsensitive);
    const bool outA = out.contains(QLatin1String("(a)"), Qt::CaseInsensitive);
    const bool outB = out.contains(QLatin1String("(b)"), Qt::CaseInsensitive);
    return (inA && outB) || (inB && outA);
#else
    // macOS: IAC 总线输入/输出同名时，发出的消息会回到自己
    const QString in = m_inputBox->currentText();
    const QString out = m_outputBox->currentText();
    return !in.isEmpty() && in == out;
#endif
}

void MainWindow::refreshDevices(bool force)
{
    const QStringList inputs = MidiEngine::inputDevices();
    const QStringList outputs = MidiEngine::outputDevices();
    static QStringList lastInputs, lastOutputs;
    if (!force && inputs == lastInputs && outputs == lastOutputs)
        return;
    lastInputs = inputs;
    lastOutputs = outputs;

    m_updatingUi = true;
    m_inputBox->clear();
    m_inputBox->addItems(inputs);
    m_outputBox->clear();
    m_outputBox->addItems(outputs);

    int idxIn = m_inputBox->findText(m_profile.inputDevice);
    if (idxIn < 0)
        idxIn = pickDevice(inputs, {"impactx", "pad"});
    int idxOut = m_outputBox->findText(m_profile.outputDevice);
    if (idxOut < 0)
        idxOut = pickDevice(outputs, {"loopback (a", "loopback"});
    if (m_inputBox->count() && idxIn >= 0)
        m_inputBox->setCurrentIndex(idxIn);
    if (m_outputBox->count() && idxOut >= 0)
        m_outputBox->setCurrentIndex(idxOut);
    m_updatingUi = false;

    if (m_inputBox->count())
        m_profile.inputDevice = m_inputBox->currentText();
    if (m_outputBox->count())
        m_profile.outputDevice = m_outputBox->currentText();
    updateDeviceHint();
    restartEngine();
}

void MainWindow::restartEngine()
{
    if (feedbackRisk()) {
        // 输入/输出为同一对环回的交叉两端会自激，拒绝启动
        m_engine->stop();
        updateStatus();
        return;
    }
    if (m_profile.inputDevice.isEmpty() || m_profile.outputDevice.isEmpty()) {
        m_engine->stop();
        updateStatus();
        return;
    }
    if (m_engine->isRunning()
        && m_engine->property("inputName").toString() == m_profile.inputDevice
        && m_engine->property("outputName").toString() == m_profile.outputDevice)
        return;  // 设备没变，不打断正在运行的引擎
    m_engine->setProperty("inputName", m_profile.inputDevice);
    m_engine->setProperty("outputName", m_profile.outputDevice);
    m_engine->start(m_profile.inputDevice, m_profile.outputDevice);
    updateStatus();
}

// ---- 打击垫编辑 ----

void MainWindow::onPadSelected(int index)
{
    m_selectedPad = index;
    fillPadEditor();
}

void MainWindow::updatePadEditorEnabled()
{
    const bool hasPad = m_selectedPad >= 0;
    const bool clip = hasPad && m_modeBox->currentIndex() == 1;
    m_sourceBox->setEnabled(hasPad);
    m_noteBox->setEnabled(hasPad && !clip);
    m_channelBox->setEnabled(hasPad);
    m_muteBox->setEnabled(hasPad);
    m_clipPathEdit->setEnabled(clip);
    m_browseClipButton->setEnabled(clip);
    m_bpmBox->setEnabled(clip);
    m_loopBox->setEnabled(clip);
}

void MainWindow::fillPadEditor()
{
    m_updatingUi = true;
    const bool hasPad = m_selectedPad >= 0;
    if (hasPad) {
        const PadMapping &m = m_profile.pads[m_selectedPad];
        m_padTitleLabel->setText(tr("<b>垫 %1</b>（源音符 %2）")
                                     .arg(m_selectedPad + 1)
                                     .arg(NoteNames::withNumber(m.source)));
        m_modeBox->setCurrentIndex(m.mode == 1 ? 1 : 0);
        m_sourceBox->setCurrentIndex(m.source);
        m_noteBox->setCurrentIndex(m.target);
        m_channelBox->setCurrentIndex(m.channel);
        m_muteBox->setChecked(m.muted);
        m_clipPathEdit->setText(m.clipPath);
        m_bpmBox->setValue(int(qBound(30.0, m.bpm, 300.0)));
        m_loopBox->setChecked(m.loop);
    } else {
        m_padTitleLabel->setText(tr("点击左侧网格选择打击垫"));
    }
    m_updatingUi = false;
    updatePadEditorEnabled();
}

void MainWindow::onPadEdited()
{
    if (m_updatingUi || m_selectedPad < 0)
        return;
    PadMapping &m = m_profile.pads[m_selectedPad];
    m.enabled = true;
    m.muted = m_muteBox->isChecked();
    m.source = quint8(qBound(0, m_sourceBox->currentIndex(), 127));
    m.target = quint8(qBound(0, m_noteBox->currentIndex(), 127));
    m.channel = quint8(qBound(0, m_channelBox->currentIndex(), 16));
    m.mode = m_modeBox->currentIndex() == 1 ? quint8(1) : quint8(0);
    m.loop = m_loopBox->isChecked();
    m.bpm = m_bpmBox->value();
    m.clipPath = m_clipPathEdit->text();
    m_grid->setProfileVisuals(m_profile);
    applyProfileToEngine();
    autosaveProfile();
}

void MainWindow::onBrowseClip()
{
    if (m_selectedPad < 0)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 MIDI 文件"), QString(),
        tr("MIDI 文件 (*.mid *.midi);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    // 选择时立即解析验证，坏文件当场报错
    QString error;
    const MidiClip clip = parseMidiFile(path, m_bpmBox->value(), &error);
    if (!clip.isValid()) {
        QMessageBox::warning(this, tr("SuperMidiMap"),
                             tr("无法解析该 MIDI 文件：\n%1").arg(error));
        return;
    }
    m_clipPathEdit->setText(path);
    onPadEdited();
}

void MainWindow::onPassthroughChanged()
{
    if (m_updatingUi)
        return;
    m_profile.passthrough = m_passthroughBox->isChecked();
    applyProfileToEngine();
    autosaveProfile();
}

void MainWindow::onPanicClicked()
{
    m_engine->panic();
}

void MainWindow::onTestOutputClicked()
{
    m_engine->sendTestNote();
}

void MainWindow::onEngineError(const QString &message)
{
    statusBar()->showMessage(message, 5000);
    updateStatus();
}

// ---- 配置档 ----

QString MainWindow::profilesDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/profiles");
}

void MainWindow::refreshProfileList()
{
    m_updatingUi = true;
    m_profileBox->clear();
    QDir dir(profilesDir());
    QStringList names;
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")},
                                                 QDir::Files, QDir::Name | QDir::IgnoreCase))
        names << fi.completeBaseName();
    m_profileBox->addItems(names);
    const int idx = m_profileBox->findText(m_currentProfileFile);
    if (idx >= 0)
        m_profileBox->setCurrentIndex(idx);
    m_updatingUi = false;
}

void MainWindow::onProfileActivated(int index)
{
    if (m_updatingUi || index < 0)
        return;
    const QString name = m_profileBox->itemText(index);
    if (name.isEmpty())
        return;
    m_currentProfileFile = name;
    loadProfileFile(profilesDir() + QStringLiteral("/") + name + QStringLiteral(".json"));
    updateDeviceHint();
}

void MainWindow::onDeleteProfile()
{
    const QString name = m_profileBox->currentText();
    if (name.isEmpty()) {
        QMessageBox::information(this, tr("SuperMidiMap"), tr("下拉框中没有可删除的配置档。"));
        return;
    }
    const auto ret = QMessageBox::question(
        this, tr("删除配置档"),
        tr("确定删除配置档 \"%1\" 吗？此操作不可恢复。").arg(name));
    if (ret != QMessageBox::Yes)
        return;
    QFile file(profilesDir() + QStringLiteral("/") + name + QStringLiteral(".json"));
    if (!file.remove()) {
        QMessageBox::warning(this, tr("SuperMidiMap"), tr("删除失败: %1").arg(file.fileName()));
        return;
    }
    if (m_currentProfileFile == name)
        m_currentProfileFile.clear();
    refreshProfileList();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 点开配置档下拉框前重新扫描目录，保证列表是最新的
    if (watched == m_profileBox && event->type() == QEvent::MouseButtonPress)
        refreshProfileList();
    return QMainWindow::eventFilter(watched, event);
}

QString MainWindow::autosavePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/autosave.json");
}

void MainWindow::autosaveProfile()
{
    QFile file(autosavePath());
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(m_profile.toJson()).toJson(QJsonDocument::Compact));
}

void MainWindow::loadProfileFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("SuperMidiMap"), tr("无法打开文件: %1").arg(path));
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    bool ok = false;
    MappingProfile loaded;
    if (parseError.error == QJsonParseError::NoError && doc.isObject())
        loaded = MappingProfile::fromJson(doc.object(), &ok);
    if (!ok) {
        QMessageBox::warning(this, tr("SuperMidiMap"),
                             tr("文件不是有效的 SuperMidiMap 配置档: %1").arg(path));
        return;
    }
    m_profile = loaded;
    syncProfileToUi();
    applyProfileToEngine();
    refreshDevices(true);
    autosaveProfile();
}

void MainWindow::onSaveProfile()
{
    const QString dir = profilesDir();
    QDir().mkpath(dir);
    const QString path = QFileDialog::getSaveFileName(this, tr("保存配置档"),
                                                      dir + QStringLiteral("/未命名.json"),
                                                      tr("SuperMidiMap 配置档 (*.json)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("SuperMidiMap"), tr("保存失败: %1").arg(path));
        return;
    }
    file.write(QJsonDocument(m_profile.toJson()).toJson());
    // 若保存在 profiles 目录中，记录为当前配置档并在下拉框中选中
    if (QFileInfo(path).absolutePath() == QDir(dir).absolutePath()) {
        m_currentProfileFile = QFileInfo(path).completeBaseName();
        refreshProfileList();
    }
}

void MainWindow::onLoadProfile()
{
    QDir().mkpath(profilesDir());
    const QString path = QFileDialog::getOpenFileName(this, tr("加载配置档"), profilesDir(),
                                                      tr("SuperMidiMap 配置档 (*.json)"));
    if (path.isEmpty())
        return;
    loadProfileFile(path);
}

void MainWindow::onResetDefaults()
{
    m_profile = MappingProfile{};
    syncProfileToUi();
    applyProfileToEngine();
    autosaveProfile();
}

void MainWindow::syncProfileToUi()
{
    m_updatingUi = true;
    m_passthroughBox->setChecked(m_profile.passthrough);
    if (m_inputBox->count() && !m_profile.inputDevice.isEmpty()) {
        const int i = m_inputBox->findText(m_profile.inputDevice);
        if (i >= 0)
            m_inputBox->setCurrentIndex(i);
    }
    if (m_outputBox->count() && !m_profile.outputDevice.isEmpty()) {
        const int i = m_outputBox->findText(m_profile.outputDevice);
        if (i >= 0)
            m_outputBox->setCurrentIndex(i);
    }
    m_updatingUi = false;
    m_grid->setProfileVisuals(m_profile);
    fillPadEditor();
}

void MainWindow::applyProfileToEngine()
{
    QString warning;
    m_engine->applyTable(m_profile.buildTable(&warning));
    if (!warning.isEmpty())
        statusBar()->showMessage(warning, 8000);
}

// ---- 状态与生命周期 ----

void MainWindow::changeEvent(QEvent *event)
{
    // 点"最小化"改为隐藏到托盘，映射继续在后台工作
    if (event->type() == QEvent::WindowStateChange && m_tray && m_tray->isVisible()
        && (windowState() & Qt::WindowMinimized)) {
        QTimer::singleShot(0, this, &QWidget::hide);
        if (!m_minimizeHintShown) {
            m_tray->showMessage(tr("SuperMidiMap"),
                                tr("已最小化到系统托盘，映射仍在工作。点击托盘图标可恢复窗口。"),
                                QSystemTrayIcon::Information, 3000);
            m_minimizeHintShown = true;
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::findDuplicateEnabledSource(int *first, int *second) const
{
    *first = *second = -1;
    for (int i = 0; i < MappingProfile::kPads; ++i) {
        if (!m_profile.pads[i].enabled)
            continue;
        for (int j = i + 1; j < MappingProfile::kPads; ++j) {
            if (m_profile.pads[j].enabled && m_profile.pads[j].source == m_profile.pads[i].source) {
                *first = i;
                *second = j;
                return;
            }
        }
    }
}

void MainWindow::updateStatus()
{
    if (!m_statusLabel)
        return;
    QString warning;
    int dupA = -1, dupB = -1;
    findDuplicateEnabledSource(&dupA, &dupB);
    if (dupA >= 0)
        warning = tr("  |  ⚠ 垫%1与垫%2源音符相同(%3)，后者覆盖前者")
                      .arg(dupA + 1)
                      .arg(dupB + 1)
                      .arg(m_profile.pads[dupB].source);
    if (m_engine->isRunning()) {
        m_statusLabel->setText(tr("● 运行中  |  输入: %1  →  输出: %2  |  已转发: %3  丢弃: %4%5")
                                   .arg(m_profile.inputDevice, m_profile.outputDevice)
                                   .arg(m_engine->messagesForwarded())
                                   .arg(m_engine->messagesDropped())
                                   .arg(warning));
    } else {
        m_statusLabel->setText(tr("○ 未运行 — 请确认打击垫已连接，并在上方选择输入/输出设备%1")
                                   .arg(warning));
    }
}

void MainWindow::persistSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
}

void MainWindow::restoreSettings()
{
    QSettings settings;
    const QByteArray geo = settings.value(QStringLiteral("geometry")).toByteArray();
    if (!geo.isEmpty())
        restoreGeometry(geo);

    QFile file(autosavePath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        bool ok = false;
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const MappingProfile loaded = MappingProfile::fromJson(doc.object(), &ok);
            if (ok)
                m_profile = loaded;
        }
    }
    m_grid->setProfileVisuals(m_profile);
    fillPadEditor();
    m_updatingUi = true;
    m_passthroughBox->setChecked(m_profile.passthrough);
    m_updatingUi = false;
    refreshProfileList();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    persistSettings();
    autosaveProfile();
    if (m_tray && m_tray->isVisible()) {
        event->ignore();
        hide();  // 最小化到托盘常驻，保持映射继续生效
        return;
    }
    event->accept();
}

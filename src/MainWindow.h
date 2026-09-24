#pragma once
#include <QMainWindow>
#include "MappingProfile.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QSystemTrayIcon;
class QTimer;
class MidiEngine;
class PadGridWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onDeviceSelectionChanged();
    void onPadSelected(int index);
    void onPadEdited();
    void onBrowseClip();
    void onPassthroughChanged();
    void onPanicClicked();
    void onTestOutputClicked();
    void onSaveProfile();
    void onLoadProfile();
    void onResetDefaults();
    void onProfileActivated(int index);
    void onDeleteProfile();
    void onEngineError(const QString &message);
    void updateStatus();

private:
    void buildUi();
    void refreshDevices(bool force);
    void updateDeviceHint();
    bool feedbackRisk() const;
    void restartEngine();
    void fillPadEditor();
    void updatePadEditorEnabled();
    void applyProfileToEngine();
    void syncProfileToUi();
    void autosaveProfile();
    void loadProfileFile(const QString &path);
    QString profilesDir() const;
    void refreshProfileList();
    QString autosavePath() const;
    void persistSettings();
    void restoreSettings();
    void findDuplicateEnabledSource(int *first, int *second) const;

    MappingProfile m_profile;
    QString m_currentProfileFile;   // 当前加载的配置档名（无扩展名），空 = 未从文件加载
    int m_selectedPad = -1;
    bool m_updatingUi = false;
    bool m_minimizeHintShown = false;

    MidiEngine *m_engine = nullptr;
    PadGridWidget *m_grid = nullptr;

    QComboBox *m_inputBox = nullptr;
    QComboBox *m_outputBox = nullptr;
    QLabel *m_deviceHint = nullptr;
    QComboBox *m_profileBox = nullptr;
    QCheckBox *m_passthroughBox = nullptr;
    QLabel *m_padTitleLabel = nullptr;
    QComboBox *m_sourceBox = nullptr;
    QComboBox *m_noteBox = nullptr;
    QComboBox *m_modeBox = nullptr;
    QLineEdit *m_clipPathEdit = nullptr;
    QPushButton *m_browseClipButton = nullptr;
    QSpinBox *m_bpmBox = nullptr;
    QCheckBox *m_loopBox = nullptr;
    QComboBox *m_channelBox = nullptr;
    QCheckBox *m_muteBox = nullptr;
    QLabel *m_statusLabel = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QTimer *m_deviceTimer = nullptr;
    QTimer *m_statusTimer = nullptr;
};

#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include "engine/input/PlaylistSource.h"
#include <memory>

class PlaylistDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlaylistDialog(std::shared_ptr<PlaylistSource> playlistSource, QWidget *parent = nullptr);
    ~PlaylistDialog() override = default;

private slots:
    void onAddFilesClicked();
    void onRemoveClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onClearClicked();
    void onItemDoubleClicked(QListWidgetItem *item);

    void onPrevTrackClicked();
    void onPlayPauseClicked();
    void onNextTrackClicked();

    void onLoopPlaylistToggled(bool checked);
    void onLoopTrackToggled(bool checked);
    void onImageDurationChanged(double val);

    void refreshTrackList();

private:
    void setupUi();

    std::shared_ptr<PlaylistSource> m_playlistSource{nullptr};

    QListWidget* m_trackListWidget{nullptr};

    QPushButton* m_addFilesBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QPushButton* m_moveUpBtn{nullptr};
    QPushButton* m_moveDownBtn{nullptr};
    QPushButton* m_clearBtn{nullptr};

    QPushButton* m_prevBtn{nullptr};
    QPushButton* m_playPauseBtn{nullptr};
    QPushButton* m_nextBtn{nullptr};

    QCheckBox* m_loopPlaylistCheckBox{nullptr};
    QCheckBox* m_loopTrackCheckBox{nullptr};
    QDoubleSpinBox* m_imageDurationSpin{nullptr};

    QLabel* m_statusLabel{nullptr};
};

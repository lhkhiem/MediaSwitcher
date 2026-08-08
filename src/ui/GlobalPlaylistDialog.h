#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLabel>
#include "engine/input/InputManager.h"
#include "engine/input/GlobalPlaylistController.h"

class GlobalPlaylistDialog : public QDialog {
    Q_OBJECT

public:
    explicit GlobalPlaylistDialog(InputManager& inputManager, GlobalPlaylistController& controller, QWidget *parent = nullptr);
    ~GlobalPlaylistDialog() override = default;

private slots:
    void onAddInputClicked();
    void onRemoveStepClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onLoopToggled(bool checked);
    void refreshTable();

private:
    void setupUi();

    InputManager& m_inputManager;
    GlobalPlaylistController& m_controller;

    QListWidget* m_availableInputsList{nullptr};
    QTableWidget* m_playlistTable{nullptr};

    QPushButton* m_addBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QPushButton* m_moveUpBtn{nullptr};
    QPushButton* m_moveDownBtn{nullptr};

    QCheckBox* m_loopCheckBox{nullptr};

    std::vector<GlobalPlaylistStep> m_currentSteps;
};

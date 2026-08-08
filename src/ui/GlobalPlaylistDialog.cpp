#include "GlobalPlaylistDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>

GlobalPlaylistDialog::GlobalPlaylistDialog(InputManager& inputManager, GlobalPlaylistController& controller, QWidget *parent)
    : QDialog(parent)
    , m_inputManager(inputManager)
    , m_controller(controller)
{
    this->setWindowTitle("Global Broadcast Playlist Config - MediaSwitcher");
    this->resize(720, 460);
    this->setStyleSheet(R"(
        QDialog {
            background-color: #161720;
            color: #FFFFFF;
        }
        QLabel { color: #CCCCCC; }
    )");

    m_currentSteps = m_controller.steps();

    setupUi();
    refreshTable();
}

void GlobalPlaylistDialog::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    QLabel* headerLabel = new QLabel("📋 Configure Global Broadcast Playlist", this);
    headerLabel->setStyleSheet("color: #00ACC1; font-weight: bold; font-size: 15px;");
    mainLayout->addWidget(headerLabel);

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(10);

    // Left Column: Available Input Channels
    QVBoxLayout* leftCol = new QVBoxLayout();
    QLabel* leftLabel = new QLabel("Available Inputs:", this);
    leftLabel->setStyleSheet("color: #4FC3F7; font-weight: bold;");
    leftCol->addWidget(leftLabel);

    m_availableInputsList = new QListWidget(this);
    m_availableInputsList->setStyleSheet(R"(
        QListWidget {
            background-color: #0E0F14; color: #FFF; border: 1px solid #2B2D3A; border-radius: 6px; padding: 4px;
        }
        QListWidget::item { padding: 6px; border-bottom: 1px solid #1C1E2A; }
        QListWidget::item:selected { background-color: #00838F; font-weight: bold; }
    )");

    const auto& slotList = m_inputManager.inputSlots();
    for (const auto& slot : slotList) {
        QString itemText = QString("Input #%1: %2").arg(slot.id).arg(QString::fromStdString(slot.name));
        QListWidgetItem* item = new QListWidgetItem(itemText, m_availableInputsList);
        item->setData(Qt::UserRole, slot.id);
    }
    leftCol->addWidget(m_availableInputsList, 1);

    m_addBtn = new QPushButton("➕ Add to Sequence ➡", this);
    m_addBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #00ACC1; color: #FFF; font-weight: bold; border: none; border-radius: 4px; padding: 8px;
        }
        QPushButton:hover { background-color: #26C6DA; }
    )");
    connect(m_addBtn, &QPushButton::clicked, this, &GlobalPlaylistDialog::onAddInputClicked);
    leftCol->addWidget(m_addBtn);

    contentLayout->addLayout(leftCol, 1);

    // Right Column: Playlist Steps Table
    QVBoxLayout* rightCol = new QVBoxLayout();
    QLabel* rightLabel = new QLabel("Playlist Sequence Steps:", this);
    rightLabel->setStyleSheet("color: #FFB74D; font-weight: bold;");
    rightCol->addWidget(rightLabel);

    m_playlistTable = new QTableWidget(0, 4, this);
    m_playlistTable->setHorizontalHeaderLabels({"Step", "Input Channel", "Duration (s)", "Transition"});
    m_playlistTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_playlistTable->setStyleSheet(R"(
        QTableWidget {
            background-color: #0E0F14; color: #FFF; border: 1px solid #2B2D3A; gridline-color: #2B2D3A;
        }
        QHeaderView::section {
            background-color: #212330; color: #00E5FF; font-weight: bold; padding: 4px; border: 1px solid #2B2D3A;
        }
    )");
    rightCol->addWidget(m_playlistTable, 1);

    // Sequence Controls (Remove, Up, Down)
    QHBoxLayout* seqCtrlLayout = new QHBoxLayout();
    m_removeBtn = new QPushButton("❌ Remove", this);
    m_removeBtn->setStyleSheet("background-color: #D32F2F; color: #FFF; font-weight: bold; border-radius: 4px; padding: 5px 10px;");
    connect(m_removeBtn, &QPushButton::clicked, this, &GlobalPlaylistDialog::onRemoveStepClicked);
    seqCtrlLayout->addWidget(m_removeBtn);

    m_moveUpBtn = new QPushButton("⬆ Move Up", this);
    m_moveUpBtn->setStyleSheet("background-color: #2B2D3A; color: #FFF; font-weight: bold; border-radius: 4px; padding: 5px 10px;");
    connect(m_moveUpBtn, &QPushButton::clicked, this, &GlobalPlaylistDialog::onMoveUpClicked);
    seqCtrlLayout->addWidget(m_moveUpBtn);

    m_moveDownBtn = new QPushButton("⬇ Move Down", this);
    m_moveDownBtn->setStyleSheet("background-color: #2B2D3A; color: #FFF; font-weight: bold; border-radius: 4px; padding: 5px 10px;");
    connect(m_moveDownBtn, &QPushButton::clicked, this, &GlobalPlaylistDialog::onMoveDownClicked);
    seqCtrlLayout->addWidget(m_moveDownBtn);

    rightCol->addLayout(seqCtrlLayout);

    contentLayout->addLayout(rightCol, 2);

    mainLayout->addLayout(contentLayout, 1);

    // Footer Options & Save
    QHBoxLayout* footerLayout = new QHBoxLayout();
    m_loopCheckBox = new QCheckBox("🔁 Loop Playlist Continuously", this);
    m_loopCheckBox->setChecked(m_controller.isLoop());
    m_loopCheckBox->setStyleSheet("color: #4FC3F7; font-weight: bold;");
    connect(m_loopCheckBox, &QCheckBox::toggled, this, &GlobalPlaylistDialog::onLoopToggled);
    footerLayout->addWidget(m_loopCheckBox);

    footerLayout->addStretch();

    QPushButton* saveBtn = new QPushButton("💾 Save & Apply", this);
    saveBtn->setFixedWidth(120);
    saveBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #388E3C; color: #FFF; font-weight: bold; padding: 8px; border-radius: 4px;
        }
        QPushButton:hover { background-color: #4CAF50; }
    )");
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        m_controller.setSteps(m_currentSteps);
        accept();
    });
    footerLayout->addWidget(saveBtn);

    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setFixedWidth(80);
    cancelBtn->setStyleSheet("background-color: #424242; color: #FFF; font-weight: bold; padding: 8px; border-radius: 4px;");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    footerLayout->addWidget(cancelBtn);

    mainLayout->addLayout(footerLayout);
}

void GlobalPlaylistDialog::refreshTable() {
    m_playlistTable->setRowCount(0);

    for (size_t i = 0; i < m_currentSteps.size(); ++i) {
        auto& step = m_currentSteps[i];
        int row = m_playlistTable->rowCount();
        m_playlistTable->insertRow(row);

        // Col 0: Step #
        QTableWidgetItem* itemStep = new QTableWidgetItem(QString("#%1").arg(i + 1));
        itemStep->setTextAlignment(Qt::AlignCenter);
        m_playlistTable->setItem(row, 0, itemStep);

        // Col 1: Input Channel Name
        QString chanName = QString("Channel #%1").arg(step.slotId);
        auto* slot = m_inputManager.getSlot(step.slotId);
        if (slot) {
            chanName = QString("Input #%1 [%2]").arg(slot->id).arg(QString::fromStdString(slot->name));
        }
        QTableWidgetItem* itemChan = new QTableWidgetItem(chanName);
        m_playlistTable->setItem(row, 1, itemChan);

        // Col 2: Duration Spin Box (0.0 = Full Video)
        QDoubleSpinBox* spinDur = new QDoubleSpinBox(this);
        spinDur->setRange(0.0, 3600.0);
        spinDur->setSingleStep(1.0);
        spinDur->setValue(step.customDurationSec);
        spinDur->setSpecialValueText("Full Video (EOF)");
        spinDur->setStyleSheet("background-color: #2B2D3A; color: #FFF;");
        connect(spinDur, &QDoubleSpinBox::valueChanged, this, [this, i](double val) {
            if (i < m_currentSteps.size()) m_currentSteps[i].customDurationSec = val;
        });
        m_playlistTable->setCellWidget(row, 2, spinDur);

        // Col 3: Transition Combo Box ("FADE", "CUT")
        QComboBox* comboTrans = new QComboBox(this);
        comboTrans->addItem("FADE 500ms", "FADE");
        comboTrans->addItem("CUT Instant", "CUT");
        int idx = (step.transitionType == "CUT") ? 1 : 0;
        comboTrans->setCurrentIndex(idx);
        comboTrans->setStyleSheet("background-color: #2B2D3A; color: #FFF;");
        connect(comboTrans, &QComboBox::currentIndexChanged, this, [this, i, comboTrans](int) {
            if (i < m_currentSteps.size()) m_currentSteps[i].transitionType = comboTrans->currentData().toString().toStdString();
        });
        m_playlistTable->setCellWidget(row, 3, comboTrans);
    }
}

void GlobalPlaylistDialog::onAddInputClicked() {
    auto* item = m_availableInputsList->currentItem();
    if (!item) return;

    int slotId = item->data(Qt::UserRole).toInt();
    if (slotId <= 0) return;

    GlobalPlaylistStep step;
    step.slotId = slotId;
    step.customDurationSec = 0.0; // Default: Full Video
    step.transitionType = "FADE";

    m_currentSteps.push_back(step);
    refreshTable();
}

void GlobalPlaylistDialog::onRemoveStepClicked() {
    int row = m_playlistTable->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_currentSteps.size()) return;

    m_currentSteps.erase(m_currentSteps.begin() + row);
    refreshTable();
}

void GlobalPlaylistDialog::onMoveUpClicked() {
    int row = m_playlistTable->currentRow();
    if (row <= 0 || static_cast<size_t>(row) >= m_currentSteps.size()) return;

    std::swap(m_currentSteps[row], m_currentSteps[row - 1]);
    refreshTable();
    m_playlistTable->setCurrentCell(row - 1, 0);
}

void GlobalPlaylistDialog::onMoveDownClicked() {
    int row = m_playlistTable->currentRow();
    if (row < 0 || static_cast<size_t>(row + 1) >= m_currentSteps.size()) return;

    std::swap(m_currentSteps[row], m_currentSteps[row + 1]);
    refreshTable();
    m_playlistTable->setCurrentCell(row + 1, 0);
}

void GlobalPlaylistDialog::onLoopToggled(bool checked) {
    m_controller.setLoop(checked);
}

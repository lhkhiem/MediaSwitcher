#pragma once

#include <QDialog>

class QTextBrowser;

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr, bool vietnamese = true);
    ~AboutDialog() override = default;

private:
    void setupUi();
    void loadContent();

    bool m_vietnamese{true};
    QTextBrowser* m_contentView{nullptr};
};

#pragma once

#include <QDialog>

class QTextBrowser;

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() override = default;

private:
    void setupUi();
    void loadContent();

    QTextBrowser* m_contentView{nullptr};
};

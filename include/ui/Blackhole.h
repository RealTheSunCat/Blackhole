#pragma once

#include <QMainWindow>
#include <QScopedPointer>
#include <QDir>
#include <string>

class QListWidgetItem;

static std::string blackholeName = "Blackhole v0.1";
extern int g_gameType;

namespace Ui {
class Blackhole;
}

class Blackhole : public QMainWindow
{
    Q_OBJECT

public:
    explicit Blackhole(QWidget *parent = nullptr);
    ~Blackhole() override;

    static QDir m_gameDir;

private:
    QScopedPointer<Ui::Blackhole> m_ui;

    void btnSelectGameDirPressed();
    void btnSettingsPressed();
    void btnAboutPressed();
    void galaxyListDoubleClicked(QListWidgetItem* item);

    void openGameDir();
};

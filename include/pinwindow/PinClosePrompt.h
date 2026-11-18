#pragma once

#include <QEventLoop>
#include <optional>

class PinClosePrompt : public QObject
{
    Q_OBJECT
public:
    enum Choice { Cancel, Save, Discard };

    Choice waitForChoice()
    {
        // Showing the native window can dispatch a decision before we get here.
        if (!m_choice) {
            m_loop.exec();
        }
        return m_choice.value_or(Cancel);
    }

    Q_INVOKABLE void cancel() { finish(Cancel); }
    Q_INVOKABLE void save() { finish(Save); }
    Q_INVOKABLE void discard() { finish(Discard); }

private:
    void finish(Choice choice)
    {
        if (!m_choice) {
            m_choice = choice;
            m_loop.quit();
        }
    }

    QEventLoop m_loop;
    std::optional<Choice> m_choice;
};

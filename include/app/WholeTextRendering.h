// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QObject>
#include <QQuickItem>

namespace freetunnel {

// Has a QML TextEdit put all of its text in the scene graph, not only the lines
// its Flickable shows. The Logs page calls it, through Backend, after every text
// it assigns.
//
// Past 10,000 characters Qt's TextEdit (6.3 on, still so in 6.10) builds nodes
// only for the blocks in view and remembers where the first of them starts. A
// whole new text that ends before that position has it skip every block: nothing
// is drawn until a scroll makes it start again (QTBUG-145449). The page replaces
// its whole text on each refresh, and the log gets shorter when the lines leaving
// the 500 it keeps are longer than the ones arriving. While the window is in the
// tray no frame is drawn, so the position goes stale across many refreshes. Drawn
// whole, the first node always starts at the top and nothing is skipped.
//
// setText() turns the culling back on whenever the text is over 10,000
// characters, so this has to come after every assignment, not before.
//
// The price is the whole log's glyphs on each refresh instead of a screenful's:
// two to three times the cost of a refresh at 500 lines, while the page is open
// and lines arrive. Making Qt forget the position would keep the culling, but
// that takes private API (QQuickTextEdit::invalidate(), which lays the whole text
// out again), or taking the TextEdit out of the window for a moment. That drops a
// press that is starting a selection, takes the keyboard focus away and back on
// every refresh, and leaves parts of the log blank when it is scrolled back up.
inline void renderWholeText(QObject *textEdit)
{
    if (auto *item = qobject_cast<QQuickItem *>(textEdit))
        item->setFlag(QQuickItem::ItemObservesViewport, false);
}

} // namespace freetunnel

#include "net/Ao3Parser.h"
#include <QDebug>
#include <cassert>

int main()
{
    // Test case 1: Real AO3 logged-in header HTML snippet for carrisa_lyna
    QByteArray loggedInHtml = R"HTML(
    <header id="header">
      <div id="greeting" class="dropdown">
        <a class="dropdown-toggle" href="/users/carrisa_lyna">
          <img alt="" class="icon" src="/images/icon.png" />
          Hi, carrisa_lyna!
        </a>
        <ul class="menu dropdown-menu" role="menu">
          <li><a href="/users/carrisa_lyna/profile">My Profile</a></li>
          <li><a href="/users/carrisa_lyna/pseuds">My Pseuds</a></li>
        </ul>
      </div>
    </header>
    <div id="main">
      <h2>Featured Works by ExitCue</h2>
      <a href="/users/ExitCue/pseuds/ExitCue">ExitCue Work</a>
    </div>
    )HTML";

    const QString user = Ao3Parser::parseUsername(loggedInHtml);
    qDebug() << "Extracted username:" << user;
    assert(user == "carrisa_lyna");
    assert(user != "ExitCue");

    qDebug() << "✅ Username parsing test passed! Correctly extracted carrisa_lyna and ignored ExitCue!";
    return 0;
}

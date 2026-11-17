#include <QtTest>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QUuid>
#include "instance.h"
class HandoffTests:public QObject {
    Q_OBJECT
private slots:
    void waitsForPreparationAndAcknowledgedOwnership(){
        QTemporaryDir directory(QDir::currentPath()+"/handoff-XXXXXX");const auto scope=QUuid::createUuid().toString();
        AppInstance old(nullptr,scope,directory.path()),replacement(nullptr,scope,directory.path());QVERIFY(old.acquire());
        QSignalSpy completed(&old,&AppInstance::handoffFinished);QLocalSocket child;QElapsedTimer preparation;preparation.start();bool owned=false,committed=false;
        connect(&child,&QLocalSocket::connected,&child,[&]{child.write("prepare\n");child.flush();});
        connect(&child,&QLocalSocket::readyRead,&child,[&]{while(child.canReadLine()){auto line=child.readLine().trimmed();if(line=="go"){QVERIFY(preparation.elapsed()>=8000);owned=replacement.acquire();QVERIFY(owned);child.write("ready\n");child.flush();}else if(line=="commit")committed=true;}});
        QVERIFY(old.beginHandoff([&]{return preparation.elapsed()>=8000;},[]{},[&](const QString &token){child.connectToServer("ThinkVantage-handoff-"+token);return true;}));
        QVERIFY(!replacement.acquire());QCOMPARE(completed.size(),0);QTRY_VERIFY_WITH_TIMEOUT(committed,12000);QVERIFY(owned);QCOMPARE(completed.size(),1);QVERIFY(completed[0][0].toBool());
    }
    void failedLaunchRetainsOwner(){
        QTemporaryDir directory(QDir::currentPath()+"/handoff-XXXXXX");const auto scope=QUuid::createUuid().toString();
        AppInstance old(nullptr,scope,directory.path()),other(nullptr,scope,directory.path());QVERIFY(old.acquire());bool cancelled=false;QSignalSpy completed(&old,&AppInstance::handoffFinished);
        QVERIFY(!old.beginHandoff([]{return true;},[&]{cancelled=true;},[](const QString &){return false;}));QVERIFY(cancelled);QCOMPARE(completed.size(),1);QVERIFY(!completed[0][0].toBool());QVERIFY(!other.acquire());
    }
    void disconnectedReplacementDoesNotExitOldOwner(){
        QTemporaryDir directory(QDir::currentPath()+"/handoff-XXXXXX");const auto scope=QUuid::createUuid().toString();
        AppInstance old(nullptr,scope,directory.path()),other(nullptr,scope,directory.path());QVERIFY(old.acquire());QLocalSocket child;bool cancelled=false;QSignalSpy completed(&old,&AppInstance::handoffFinished);
        connect(&child,&QLocalSocket::connected,&child,[&]{child.write("prepare\n");child.flush();QTimer::singleShot(30,&child,&QLocalSocket::abort);});
        QVERIFY(old.beginHandoff([]{return false;},[&]{cancelled=true;},[&](const QString &token){child.connectToServer("ThinkVantage-handoff-"+token);return true;}));QTRY_VERIFY_WITH_TIMEOUT(cancelled,3000);QCOMPARE(completed.size(),1);QVERIFY(!completed[0][0].toBool());QVERIFY(!other.acquire());
    }
    void failedReplacementAfterReleaseReclaimsLock(){
        QTemporaryDir directory(QDir::currentPath()+"/handoff-XXXXXX");const auto scope=QUuid::createUuid().toString();
        AppInstance old(nullptr,scope,directory.path()),other(nullptr,scope,directory.path());QVERIFY(old.acquire());
        auto replacement=std::make_unique<AppInstance>(nullptr,scope,directory.path());QLocalSocket child;bool cancelled=false;QSignalSpy completed(&old,&AppInstance::handoffFinished);
        connect(&child,&QLocalSocket::connected,&child,[&]{child.write("prepare\n");child.flush();});
        connect(&child,&QLocalSocket::readyRead,&child,[&]{if(child.canReadLine()&&child.readLine().trimmed()=="go"){QVERIFY(replacement->acquire());replacement.reset();child.abort();}});
        QVERIFY(old.beginHandoff([]{return true;},[&]{cancelled=true;},[&](const QString &token){child.connectToServer("ThinkVantage-handoff-"+token);return true;}));
        QTRY_VERIFY_WITH_TIMEOUT(cancelled,3000);QCOMPARE(completed.size(),1);QVERIFY(!completed[0][0].toBool());QVERIFY(!other.acquire());
    }
};
QTEST_GUILESS_MAIN(HandoffTests)
#include "handoff_tests.moc"

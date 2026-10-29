#include <sailfishapp.h>

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>

#include "controller.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    Controller controller;
    view->rootContext()->setContextProperty(QStringLiteral("sailtrack"), &controller);
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return app->exec();
}

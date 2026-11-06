QT += core gui qml quick quickcontrols2 svg network

CONFIG += c++17 release
TARGET = omarchy-control
TEMPLATE = app

VERSION = 0.1.0
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

include(omarchy-control-sources.pri)

SOURCES += src/main.cpp

RESOURCES += src/resources.qrc src/qml.qrc

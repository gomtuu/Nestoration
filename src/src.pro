TEMPLATE = app
TARGET = nestoration

QT += quick
QT += widgets
QT += multimedia

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Refer to the documentation for the
# deprecated API to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += $$PWD/../libgme

SOURCES += \
        audiofile.cpp \
        channelmodel.cpp \
        generator.cpp \
        main.cpp \
        miniapu.cpp \
        nsfaudiofile.cpp \
        nsfpcm.cpp \
        player.cpp \
        squarechannel.cpp \
        toneobject.cpp \
        trianglechannel.cpp

RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Additional import path used to resolve QML modules just for Qt Quick Designer
QML_DESIGNER_IMPORT_PATH =

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    audiofile.h \
    channelmodel.h \
    generator.h \
    miniapu.h \
    nsfaudiofile.h \
    nsfpcm.h \
    player.h \
    squarechannel.h \
    toneobject.h \
    trianglechannel.h

unix: LIBS += -larchive
macx: LIBS += -larchive

LIBS += -L$$OUT_PWD/../libgme -lgme

unix: LIBS += -lsoxr
macx: LIBS += -lsoxr

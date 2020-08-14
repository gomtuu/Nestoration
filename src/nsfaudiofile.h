#ifndef NSFAUDIOFILE_H
#define NSFAUDIOFILE_H

#include "audiofile.h"
#include "gme/gme.h"

class NsfAudioFile : public AudioFile
{
    Q_OBJECT

public:
    explicit NsfAudioFile(int sample_rate, QObject *parent = 0);
    ~NsfAudioFile();

    int choose_track();
    void open(QString file_name);
    void read_gme_buffer();
    void convert_apulog_to_runs();

signals:
    void emuChanged(Music_Emu *emu);
    void fileOpened(QString file_name, qint64 file_track);

public slots:
    void openClicked();

private:
    Music_Emu *emu { nullptr };
    const int blipbuf_sample_rate;
    qint64 file_track = -1;
};

#endif // NSFAUDIOFILE_H

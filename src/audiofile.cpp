#include <QFileDialog>
#include <QDebug>

#include <archive.h>
#include <archive_entry.h>

#include "audiofile.h"
#include "channelmodel.h"
#include "toneobject.h"

AudioFile::AudioFile(QObject *parent)
    : QObject(parent), lowest_tone(8), highest_tone(8+88)
{
    this->channel0 = new ChannelModel;
    this->channel1 = new ChannelModel;
    this->channel2 = new ChannelModel;
}

void AudioFile::open(QString file_name)
{
    if (file_name == "") {
        return;
    }
    if (this->is_open) {
        this->close();
    }
    struct archive_entry *entry;
    int result;
    WAVheader header;
    m_archive = archive_read_new();
    archive_read_support_filter_gzip(m_archive);
    archive_read_support_filter_xz(m_archive);
    archive_read_support_format_raw(m_archive);
    result = archive_read_open_filename(m_archive, qPrintable(file_name), 1048576);
    if (result != ARCHIVE_OK) {
        qDebug() << archive_error_string(m_archive);
    }
    if (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK) {
        archive_read_data(m_archive, &header, 44);
        if (header.audio_format != 1) throw 3;
        if (header.num_channels != 5) throw 3;
        if (header.sample_rate != 1789773) throw 3;
        if (header.bits_per_sample != 8) throw 3;
        this->is_open = true;
    }
    if (!this->is_open) {
        this->close();
    }
}

void AudioFile::read_block(char block[], std::streamsize &bytes_read) {
    bytes_read = archive_read_data(m_archive, block, 1789773 * 5);
}

void AudioFile::openClicked()
{
    QString nes_dir = QDir::homePath() + QString("/storage/audio/emu/nes");
    QString file_name = QFileDialog::getOpenFileName(nullptr, "Open a NES music file", nes_dir, this->file_types);
    //QString file_name = QDir::homePath() + QString("/storage/audio/emu/nes/Disney's DuckTales (Released Version) (NTSC) (SFX).nsf");
    this->open(file_name);
    if (this->is_open) {
        qDebug() << "Reading runs...";
        this->read_runs();
        this->process_runs();
    }
}

void AudioFile::read_runs() {
    const uint8_t CHANNELS = 5;
    Run run[CHANNELS];
    samplevalue *block = new samplevalue[1789773 * 5];
    std::streamsize bytes_read = 0;
    sampleoff file_sample_i = 0;
    std::streamoff block_offset = 0;
    samplevalue previous_value[CHANNELS];
    samplevalue new_value[CHANNELS];

    this->read_block(reinterpret_cast<char*>(block), bytes_read);
    if (bytes_read == 0) {
        return;
    }
    this->channel_runs.clear();
    for (int channel_i = 0; channel_i < CHANNELS; channel_i += 1) {
        QList<Run> run_list;
        this->channel_runs.append(run_list);
        new_value[channel_i] = block[block_offset + channel_i];
        samplevalue raw_value = new_value[channel_i] - 128;
        if (channel_i < 4) {
            raw_value = raw_value >> 3;
        }
        run[channel_i] = { file_sample_i, 0, raw_value };
        previous_value[channel_i] = new_value[channel_i];
    }
    file_sample_i += 1;
    block_offset += 5;
    while (bytes_read) {
        while (block_offset < bytes_read) {
            for (int channel_i = 0; channel_i < CHANNELS; channel_i += 1) {
                new_value[channel_i] = block[block_offset + channel_i];
                if (new_value[channel_i] != previous_value[channel_i]) {
                    run[channel_i].length = file_sample_i - run[channel_i].start;
                    this->channel_runs[channel_i].append(run[channel_i]);
                    samplevalue raw_value = new_value[channel_i] - 128;
                    if (channel_i < 4) {
                        raw_value = raw_value >> 3;
                    }
                    run[channel_i] = { file_sample_i, 0, raw_value };
                    previous_value[channel_i] = new_value[channel_i];
                }
            }
            file_sample_i += 1;
            block_offset += 5;
        }
        block_offset = 0;
        this->read_block(reinterpret_cast<char*>(block), bytes_read);
    }
    for (int channel_i = 0; channel_i < CHANNELS; channel_i += 1) {
        run[channel_i].length = file_sample_i - run[channel_i].start;
        this->channel_runs[channel_i].append(run[channel_i]);
        qDebug() << "Channel" << channel_i << "run count:" << this->channel_runs[channel_i].size();
    }
    delete[] block;
    this->close();
}

void AudioFile::process_runs() {
    qDebug() << "Converting runs to cycles...";
    this->highest_tone = -999;
    this->lowest_tone = 999;
    for (uint8_t channel_i = 0; channel_i < 5; channel_i += 1) {
        switch (channel_i) {
            case 0:
            case 1: {
                QVector<Cycle> cycles = this->square_channels[channel_i].runs_to_cycles(this->channel_runs[channel_i]);
                QVector<ToneObject> tones { this->square_channels[channel_i].find_tones(cycles) };
                this->square_channels[channel_i].fix_transitional_tones(tones);
                this->square_channels[channel_i].fix_trailing_tones(tones);
                this->square_channels[channel_i].fix_leading_tones(tones);
                if (channel_i == 0) {
                    this->channel0->set_tones(tones);
                    emit this->channel0Changed(this->channel0);
                    this->determine_range(tones);
                } else if (channel_i == 1) {
                    this->channel1->set_tones(tones);
                    emit this->channel1Changed(this->channel1);
                    this->determine_range(tones);
                    break;
                }            }
            break;
            case 2:
                QVector<Cycle> cycles = this->triangle_channel.runs_to_cycles(this->channel_runs[channel_i]);
                QVector<ToneObject> tones { this->triangle_channel.find_tones(cycles) };
                this->channel2->set_tones(tones);
                emit this->channel2Changed(this->channel2);
                this->determine_range(tones);
            break;
        }
    }
    emit this->lowestToneChanged(this->lowest_tone);
    emit this->highestToneChanged(this->highest_tone);
    emit this->channelRunsChanged(this->channel_runs);
}

void AudioFile::determine_range(QVector<ToneObject> &tones) {
    for (ToneObject &tone: tones) {
        if (tone.shape == CycleShape::Irregular || tone.semitone_id < 0 || tone.volume == 0) {
            continue;
        }
        if (ceil(tone.semitone_id) > this->highest_tone) {
            this->highest_tone = ceil(tone.semitone_id);
        }
        if (floor(tone.semitone_id) < this->lowest_tone) {
            this->lowest_tone = floor(tone.semitone_id);
        }
    }
}

void AudioFile::close() {
    int result;
    result = archive_read_free(m_archive);
    if (result != ARCHIVE_OK)
        throw 2;
    this->is_open = false;
}

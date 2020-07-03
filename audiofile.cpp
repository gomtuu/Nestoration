#include <iostream>
#include <algorithm>

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
}

void AudioFile::open(const char *file_name)
{
    struct archive_entry *entry;
    int result;
    WAVheader header;
    m_archive = archive_read_new();
    archive_read_support_filter_gzip(m_archive);
    archive_read_support_format_raw(m_archive);
    result = archive_read_open_filename(m_archive, file_name, 1048576);
    if (result != ARCHIVE_OK) {
        std::cout << archive_error_string(m_archive) << std::endl;
        throw 1;
    }
    if (archive_read_next_header(m_archive, &entry) == ARCHIVE_OK) {
        archive_read_data(m_archive, &header, 44);
        if (header.audio_format != 1) throw 3;
        if (header.num_channels != 5) throw 3;
        if (header.sample_rate != 1789773) throw 3;
        if (header.bits_per_sample != 8) throw 3;
    }
}

void AudioFile::read_block(char block[], std::streamsize &bytes_read) {
    bytes_read = archive_read_data(m_archive, block, 1789773 * 5);
}

void AudioFile::openClicked()
{
    QString file_name = QFileDialog::getOpenFileName(nullptr, "Open a gzipped 5-channel WAV file", QString(), "gzipped WAV (*.wav.gz)");
    try {
        this->open(qPrintable(file_name));
    } catch (int e) {
        qDebug() << "Failed to open WAV file." << endl;
        return;
    }
    qDebug() << "Reading runs..." << endl;
    QVector<Run> runs = this->read_runs();
    qDebug() << "Converting runs to cycles..." << endl;
    QVector<Cycle> cycles = this->runs_to_cycles(runs);
    qDebug() << "Finding tones..." << endl;
    QVector<ToneObject> tones { this->find_tones(cycles) };
    this->channel0 = new ChannelModel { tones };
    emit this->channel0Changed(this->channel0);
    this->determine_range(tones);
    emit this->lowestToneChanged(this->lowest_tone);
    emit this->highestToneChanged(this->highest_tone);
}

double period_to_semitone(const samplesize &period) {
    double frequency = CPU_FREQENCY / period;
    return log(frequency / C0) / log(TWELFTH_ROOT);
}

QVector<Run> AudioFile::read_runs() {
    QVector<Run> runs;
    Run run;
    char *block = new char[1789773 * 5];
    std::streamsize bytes_read = 0;
    sampleoff file_sample_i = 0;
    std::streamoff block_i = 0;
    bool previous_value;
    bool new_value;

    this->read_block(block, bytes_read);
    if (bytes_read == 0) {
        return runs;
    }
    new_value = block[block_i] != -128;
    run = { file_sample_i, 0, new_value };
    previous_value = new_value;
    file_sample_i += 1;
    block_i += 5;
    while (bytes_read) {
        while (block_i < bytes_read) {
            new_value = block[block_i] != -128;
            if (new_value != previous_value) {
                run.length = file_sample_i - run.start;
                runs.append(run);
                run = { file_sample_i, 0, new_value };
            }
            previous_value = new_value;
            file_sample_i += 1;
            block_i += 5;
        }
        block_i = 0;
        this->read_block(block, bytes_read);
    }
    run.length = file_sample_i - run.start;
    runs.append(run);
    qDebug() << run.start << run.length << run.on;
    delete[] block;
    this->close();
    qDebug() << "Run count: " << runs.size() << endl;
    return runs;
}

QVector<Cycle> AudioFile::runs_to_cycles(QVector<Run> &runs) {
    QVector<Cycle> cycles;
    const Cycle clear_cycle = { 0, CycleDuty::Irregular, -999, {} };
    Cycle cycle;
    samplesize cycle_length;
    samplesize runs_total;
    bool is_normal_size;
    for (int i=0; i+1 < runs.size(); i += 1) {
        cycle = clear_cycle;
        cycle.start = runs[i].start;
        if (runs[i].on) {
            cycle_length = runs[i].length + runs[i+1].length;
            is_normal_size = (144 <= cycle_length && cycle_length <= 32768 && (cycle_length & 15) == 0);
            if (is_normal_size) {
                cycle.runs = { runs[i].length, runs[i+1].length };
                if (runs[i].length * 7 == runs[i+1].length) {
                    cycle.duty = CycleDuty::Eighth;
                } else if (runs[i].length * 3 == runs[i+1].length) {
                    cycle.duty = CycleDuty::Quarter;
                } else if (runs[i].length == runs[i+1].length) {
                    cycle.duty = CycleDuty::Half;
                } else if (runs[i].length == runs[i+1].length * 3) {
                    cycle.duty = CycleDuty::ThreeQuarters;
                }
                cycle.semitone_id = period_to_semitone(cycle_length);
                i += 1;
            } else if (cycle_length <= 32768) {
                cycle.runs = { runs[i].length, runs[i+1].length };
                cycle.semitone_id = period_to_semitone(cycle_length);
                i += 1;
            } else {
                cycle.runs = { runs[i].length };
                cycle.duty = CycleDuty::Irregular;
            }
            cycles.append(cycle);
        } else {
            cycle.runs = { runs[i].length };
            if (runs[i].length > 28672) {
                cycle.duty = CycleDuty::None;
            }
            cycles.append(cycle);
        }
        runs_total = 0;
        for (samplesize &len: cycle.runs) {
            runs_total += len;
        }
        qDebug() << cycle.start << cycle.duty << cycle.semitone_id << cycle_length << cycle.runs.count() << runs_total;
    }
    qDebug() << "Cycle count: " << cycles.size();
    return cycles;
}

QVector<ToneObject> AudioFile::find_tones(QVector<Cycle> &cycles) {
    QVector<ToneObject> tones;
    ToneObject tone;
    sampleoff tone_start;
    if (cycles.size() == 0) return tones;
    tone_start = cycles[0].start;
    tone.semitone_id = cycles[0].semitone_id;
    tone.duty = cycles[0].duty;
    for (int i = 1; i < cycles.size(); i++) {
        if (cycles[i].semitone_id != tone.semitone_id || cycles[i].duty != tone.duty) {
            tone.length = cycles[i].start - tone_start;
            qDebug() << "Semitone" << tone.semitone_id << "for" << tone.length / 1789773.0 << "sec";
            tones.append(tone);
            tone_start = cycles[i].start;
            tone = ToneObject { cycles[i].semitone_id, cycles[i].duty };
        }
    }
    for (samplesize &len: cycles.last().runs) {
        tone.length += len;
    }
    qDebug() << "Semitone" << tone.semitone_id << "for" << tone.length / 1789773.0 << "sec";
    tones.append(tone);
    qDebug() << "Tone count: " << tones.size() << endl;
    return tones;
}


void AudioFile::determine_range(QVector<ToneObject> &tones) {
    ToneObject *tone;
    int max_semitone = -999;
    int min_semitone = 999;
    for (int i = 0; i < tones.size(); i++) {
        tone = &tones[i];
        if (ceil(tone->semitone_id) > max_semitone) {
            max_semitone = ceil(tone->semitone_id);
        }
        if (floor(tone->semitone_id) < min_semitone && floor(tone->semitone_id) >= 0) {
            min_semitone = floor(tone->semitone_id);
        }
    }
    this->highest_tone = max_semitone;
    this->lowest_tone = min_semitone;
}

void AudioFile::close() {
    int result;
    result = archive_read_free(m_archive);
    if (result != ARCHIVE_OK)
        throw 2;
}

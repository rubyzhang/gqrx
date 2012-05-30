/* -*- c++ -*- */
/*
 * Copyright 2011 Alexandru Csete OZ9AEC.
 *
 * Gqrx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * Gqrx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Gqrx; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include <QtGlobal>
#include <QSettings>
#include <QFile>
#include <QString>
#include <QRegExp>
#include <QVariant>
#include <QDebug>

#include <osmosdr_device.h>
#include <osmosdr_source_c.h>
#include <osmosdr_ranges.h>
#include <boost/foreach.hpp>

#include "pulseaudio/pa_device_list.h"
#include "qtgui/ioconfig.h"
#include "ui_ioconfig.h"



CIoConfig::CIoConfig(QSettings *settings, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CIoConfig)
{
    unsigned int i=0;
    QRegExp rx("'([a-zA-Z0-9 \\-\\_\\/\\.\\,\\(\\)]+)'"); // extracts device description
    QString devstr;
    QString devlabel;
    bool cfgmatch=false; //flag to indicate that device from config was found

    ui->setupUi(this);

    QString indev = settings->value("input/device", "").toString();

    // Get list of input devices and store them in the input
    // device selector together with the device descriptor strings
    osmosdr::devices_t devs = osmosdr::device::find();

    qDebug() << __FUNCTION__ << ": Available input devices:";
    BOOST_FOREACH(osmosdr::device_t &dev, devs)
    {
        devstr = QString(dev.to_string().c_str());
        if (rx.indexIn(devstr, 0) != -1)
            devlabel = rx.cap(1);
        else
            devlabel = "Unknown";

        ui->inDevCombo->addItem(devlabel, QVariant(devstr));

        // is this the device stored in config?
        if (indev == devstr)
        {
            ui->inDevCombo->setCurrentIndex(i);
            ui->inDevEdit->setText(devstr);
            cfgmatch = true;
        }

        qDebug() << "   " << i++ << ":"  << devlabel;

        // Following code could be used for multiple matches
        /* QStringList list;
        int pos = 0;
        while ((pos = rx.indexIn(devstr, pos)) != -1) {
            list << rx.cap(1);
            pos += rx.matchedLength();
        } */

    }
    ui->inDevCombo->addItem(tr("Other..."), QVariant(""));

    // If device string from config is not one of the detected devices
    // we select the "Other..." entry in the combo box.
    if (!cfgmatch)
    {
        ui->inDevCombo->setCurrentIndex(i);
        ui->inDevEdit->setText(indev);
    }
    updateInputSampleRates(settings->value("input/sample_rate", 0).toInt());

    // Output device
    QString outdev = settings->value("output/device", "").toString();

#ifdef Q_OS_LINUX
    // get list of output devices
    pa_device_list devices;
    outDevList = devices.get_output_devices();

    qDebug() << __FUNCTION__ << ": Available output devices:";
    for (i = 0; i < outDevList.size(); i++)
    {
        qDebug() << "   " << i << ":" << QString(outDevList[i].get_description().c_str());
        //qDebug() << "     " << QString(outDevList[i].get_name().c_str());
        ui->outDevCombo->addItem(QString(outDevList[i].get_description().c_str()));

        // note that item #i in devlist will be item #(i+1)
        // in combo box due to "default"
        if (outdev == QString(outDevList[i].get_name().c_str()))
            ui->outDevCombo->setCurrentIndex(i+1);
    }

#elif defined(__APPLE__) && defined(__MACH__) // Works for X11 Qt on Mac OS X too
    //QString indev = settings.value("input", "").toString();
    //QString outdev = settings.value("output", "").toString();
#endif

    // Signals and slots
    connect(this, SIGNAL(accepted()), this, SLOT(saveConfig()));
    connect(ui->inDevCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(inputDeviceSelected(int)));

}

CIoConfig::~CIoConfig()
{
    delete ui;
}


/*! \brief Utility function to find device name of the Funcube Dogle.
 *
 * Linux: Get list of pulseaudio sources and search for "FUNcube Dongle" in the description
 * Mac: tbd...
 */
QString CIoConfig::getFcdDeviceName()
{
    QString retval("");

#ifdef Q_OS_LINUX
    pa_device_list devices;
    vector<pa_device> devlist = devices.get_input_devices();
    unsigned int i;

    qDebug() << __FUNCTION__ << ": Available input devices";
    for (i = 0; i < devlist.size(); i++)
    {
        qDebug() << "    " << i << ":" << QString(devlist[i].get_description().c_str());
        if (QString(devlist[i].get_description().c_str()).contains("FUNcube Dongle"))
        {
            retval = QString(devlist[i].get_name().c_str());
        }
    }

#elif defined(__APPLE__) && defined(__MACH__) // Works for X11 Qt on Mac OS X too
    // TODO
#endif

    qDebug() << "  => " << retval;

    return retval;
}


/*! \brief Save configuration. */
void CIoConfig::saveConfig()
{
    int idx=ui->outDevCombo->currentIndex();

    if (idx > 0)
    {
        //m_settings->setValue("output/device", ui->outDevCombo->currentText());

        qDebug() << "Output device" << idx << ":" << QString(outDevList[idx-1].get_name().c_str());
        m_settings->setValue("output/device", QString(outDevList[idx-1].get_name().c_str()));
    }
    else
        qDebug() << "Selected output device is 'default' (not saving)";


    //settings.setValue("input", ui->inDevEdit->text());
    //settings.setValue("output", ui->outDevEdit->text());
}


/*! \brief Update list of sample rates based on selected device.
 *  \param rate The current sample rate from the configuration.
 */
void CIoConfig::updateInputSampleRates(int rate)
{
    ui->inSrCombo->clear();

    if (ui->inDevEdit->text().isEmpty())
    {
        return;
    }

    /** FIXME: this code crashes on RTL device so we use fixed rates **/
    //osmosdr_source_c_sptr src = osmosdr_make_source_c(ui->inDevEdit->text().toStdString());
    //osmosdr::meta_range_t rates = src->get_sample_rates();
    //BOOST_FOREACH(osmosdr::range_t &rate, rates)
    //{
    //    ui->inSrCombo->addItem(QString("%1 kHz").arg(rate.start()/1000, 0, 'f', 0));
    //}
    //src.reset();

    if (rate > 0)
        ui->inSrCombo->addItem(QString(rate));
    else if (ui->inDevEdit->text().contains("fcd"))
        ui->inSrCombo->addItem("96000");
    else if (ui->inDevEdit->text().contains("rtl"))
        ui->inSrCombo->addItem("1200000");

}

/*! \brief New input device has been selected by the user.
 *  \param index The index of the item that has been selected in the combo box.
 */
void CIoConfig::inputDeviceSelected(int index)
{
    qDebug() << "New input device selected:" << index;
    qDebug() << "  Label:" << ui->inDevCombo->itemText(index);
    qDebug() << "  Devstr:" << ui->inDevCombo->itemData(index).toString();

    ui->inDevEdit->setText(ui->inDevCombo->itemData(index).toString());
    updateInputSampleRates(0);
}

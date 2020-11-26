/******************************************************************************
** Copyright (C) 2020 Laird Connectivity
**
** Project: XModemUtil
**
** Module: MainWindow.cpp
**
** Notes:
**
** License: This program is free software: you can redistribute it and/or
**          modify it under the terms of the GNU General Public License as
**          published by the Free Software Foundation, version 3.
**
**          This program is distributed in the hope that it will be useful,
**          but WITHOUT ANY WARRANTY; without even the implied warranty of
**          MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**          GNU General Public License for more details.
**
**          You should have received a copy of the GNU General Public License
**          along with this program.  If not, see http://www.gnu.org/licenses/
**
*******************************************************************************/

/******************************************************************************/
// Include Files
/******************************************************************************/
#include "UwxMainWindow.h"
#include "ui_UwxMainWindow.h"
#include <QMessageBox>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QCryptographicHash>

/******************************************************************************/
// Conditional Compile Defines
/******************************************************************************/
#ifdef QT_DEBUG
    //Include debug output when compiled for debugging
    #include <QDebug>
#endif
#ifdef _WIN32
    //Windows
    #ifdef _WIN64
        //Windows 64-bit
        #define OS "Windows (x86_64)"
    #else
        //Windows 32-bit
        #define OS "Windows (x86)"
    #endif
#elif defined(__APPLE__)
    #include "TargetConditionals.h"
    #ifdef TARGET_OS_MAC
        //Mac OSX
        #define OS "Mac"
        QString gstrMacBundlePath;
    #endif
#else
    //Assume Linux
    #ifdef __aarch64__
        //ARM64
        #define OS "Linux (AArch64)"
    #elif __arm__
        //ARM
        #define OS "Linux (ARM)"
    #elif __x86_64__
        //x86_64
        #define OS "Linux (x86_64)"
    #elif __i386
        //x86
        #define OS "Linux (x86)"
    #else
        //Unknown
        #define OS "Linux (other)"
    #endif
#endif

/******************************************************************************/
// Local Functions or Private Members
/******************************************************************************/
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    if (!QDir().exists(QStandardPaths::writableLocation(QStandardPaths::DataLocation)))
    {
        //Create download directory
        QDir().mkdir(QStandardPaths::writableLocation(QStandardPaths::DataLocation));
    }

#ifdef TARGET_OS_MAC
    //Fix mac's resize
//    resize(740, 400);
#endif

    //Connect serial signals
    connect(&spSerialPort, SIGNAL(readyRead()), this, SLOT(SerialRead()));
    connect(&spSerialPort, SIGNAL(error(QSerialPort::SerialPortError)), this, SLOT(SerialError(QSerialPort::SerialPortError)));
    connect(&spSerialPort, SIGNAL(bytesWritten(qint64)), this, SLOT(SerialBytesWritten(qint64)));

    //Connect timer signals
    connect(&tmrBootloaderEntranceTimer, SIGNAL(timeout()), this, SLOT(BootloaderEntranceTimerTimeout()));
    tmrBootloaderEntranceTimer.setSingleShot(false);

    //Set default UI elements
    ui->combo_Baud->setCurrentIndex(ComboBaudRateIndex115200);
    ui->combo_Handshake->setCurrentIndex(ComboBaudRateHandshakingHardware);

    //Create and setup objects
    nCPacket = XMODEM_FIRST_PACKET_ID;
    nmManager = new QNetworkAccessManager();
    connect(nmManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(replyFinished(QNetworkReply*)));
#ifdef UseSSL
    connect(nmManager, SIGNAL(sslErrors(QNetworkReply*, QList<QSslError>)), this, SLOT(sslErrors(QNetworkReply*, QList<QSslError>)));
#endif

    //Display version
    ui->statusBar->showMessage(QString("XModemUtil")
#ifdef UseSSL
    .append(" (with SSL)")
#endif
    .append(" version ").append(strUtilVersion).append(" (").append(OS).append("), Built ").append(__DATE__).append(" Using QT ").append(QT_VERSION_STR)
#ifdef UseSSL
#ifdef TARGET_OS_MAC
    .append(", ").append(QString(QSslSocket::sslLibraryBuildVersionString()).replace(",", ":"))
#else
    .append(", ").append(QString(QSslSocket::sslLibraryBuildVersionString()).left(QSslSocket::sslLibraryBuildVersionString().indexOf(" ", 9)))
#endif
#endif
#ifdef QT_DEBUG
    .append(" [DEBUG BUILD]")
#endif
    );
    setWindowTitle(QString(windowTitle()).append(" (v").append(strUtilVersion).append(")"));

    //Initialise popup message
    pmErrorForm = new PopupMessage(this);

    //Set which options are enabled
    if (ui->radio_LocalFile->isChecked())
    {
        on_radio_LocalFile_toggled(true);
    }
    else if (ui->radio_Online->isChecked())
    {
        on_radio_Online_toggled(true);
    }

#ifdef UseSSL
    //Load SSL certificate
    QFile certFile(":/certificates/UwTerminalX_new.crt");
    if (certFile.open(QIODevice::ReadOnly))
    {
        //Load certificate data
        sslcLairdConnectivity = new QSslCertificate(certFile.readAll());
        QSslSocket::addDefaultCaCertificate(*sslcLairdConnectivity);
        certFile.close();
    }
#else
    ui->check_SSL->setEnabled(false);
    ui->check_SSL->setChecked(false);
#endif

    //Populate the list of devices
    RefreshSerialDevices();
}

//=============================================================================
//=============================================================================
MainWindow::~MainWindow(
    )
{
    //Disconnect signals
    disconnect(this, SLOT(SerialRead()));
    disconnect(this, SLOT(SerialError(QSerialPort::SerialPortError)));
    disconnect(this, SLOT(SerialBytesWritten(qint64)));
    disconnect(this, SLOT(BootloaderEntranceTimerTimeout()));
    disconnect(this, SLOT(replyFinished(QNetworkReply*)));
#ifdef UseSSL
    disconnect(this, SLOT(sslErrors(QNetworkReply*, QList<QSslError>)));
#endif

#ifdef UseSSL
    if (sslcLairdConnectivity != NULL)
    {
        //Clear up SSL certificate
        delete sslcLairdConnectivity;
    }
#endif

    if (spSerialPort.isOpen())
    {
        spSerialPort.close();
    }

    if (nmManager != NULL)
    {
        //Clear up network manager
        delete nmManager;
        nmManager = NULL;
    }

    if (pmErrorForm != NULL)
    {
        delete pmErrorForm;
        pmErrorForm = NULL;
    }

    delete ui;
}

//=============================================================================
//=============================================================================
void
MainWindow::RefreshSerialDevices(
    )
{
    //Clears and refreshes the list of serial devices
    QString strPrev = "";
    QRegularExpression reTempRE("^(\\D*?)(\\d+)$");
    QList<int> lstEntries;
    lstEntries.clear();
    bool bHadDevice = false;

    if (ui->combo_COM->count() > 0)
    {
        //Remember previous option
        strPrev = ui->combo_COM->currentText();
        bHadDevice = true;
    }

    ui->combo_COM->clear();
    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
        QRegularExpressionMatch remTempREM = reTempRE.match(info.portName());
        if (remTempREM.hasMatch() == true)
        {
            //Can sort this item
            int i = lstEntries.count()-1;

            while (i >= 0)
            {
                if (remTempREM.captured(REGEX_SERIAL_INDEX_PORT).toInt() > lstEntries[i])
                {
                    //Found correct order position, add here
                    ui->combo_COM->insertItem(i+1, info.portName());
                    lstEntries.insert(i+1, remTempREM.captured(REGEX_SERIAL_INDEX_PORT).toInt());
                    i = INDEX_NOT_FOUND;
                }
                --i;
            }

            if (i == INDEX_NOT_FOUND)
            {
                //Position not found, add to beginning
                ui->combo_COM->insertItem(0, info.portName());
                lstEntries.insert(0, remTempREM.captured(REGEX_SERIAL_INDEX_PORT).toInt());
            }
        }
        else
        {
            //Cannot sort this item
            ui->combo_COM->insertItem(ui->combo_COM->count(), info.portName());
        }
    }

    //Search for previous item if one was selected
    if (strPrev == "")
    {
        //Select first item
        ui->combo_COM->setCurrentIndex(0);
    }
    else
    {
        //Search for previous
        int i = 0;
        while (i < ui->combo_COM->count())
        {
            if (ui->combo_COM->itemText(i) == strPrev)
            {
                //Found previous item
                ui->combo_COM->setCurrentIndex(i);
                break;
            }
            ++i;
        }
    }

    if (bHadDevice == true || ui->combo_COM->count() > 0)
    {
        //Update serial port info
        on_combo_COM_currentIndexChanged(0);
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::SerialRead(
    )
{
    //Receive all data from buffer
    QByteArray baRecData = spSerialPort.readAll();

    if (nAppMode == ApplicationModeTypes::ApplicationModeTypeFirmwareUpdate)
    {
        //Firmware upgrade mode
        if (nAction == ActionModeTypeXModemWaitForNack || nAction == ActionModeTypes::ActionModeTypeXModemSendData)
        {
            if (baRecData.at(0) == XModemPacketTypes::XModemPacketTypeAck)
            {
                //XModem ACK
                ui->edit_Log->appendPlainText("Got ACK");

                //Load next packet
                baLastPacket.clear();
                unsigned char cTmp;
                cTmp = XModemPacketTypes::XModemPacketType1024BytePacket;
                baLastPacket.append(cTmp);

                cTmp = nCPacket;
                baLastPacket.append(cTmp);
                cTmp = XMODEM_INVERSE - nCPacket;
                baLastPacket.append(cTmp);
                if (fpFirmwareFile.pos() != nCFilePos)
                {
                    fpFirmwareFile.seek(nCFilePos);
                }

                baLastPacket.append(fpFirmwareFile.read(nXModemDataSize));
                if (baLastPacket.length() != nXModemHeaderSize)
                {
                    if (baLastPacket.length() < (nXModemDataSize + nXModemHeaderSize))
                    {
                        //Pad with spaces
                        baLastPacket.append(QByteArray((nXModemDataSize + nXModemHeaderSize) - baLastPacket.length(), nXModemPaddingCharacter));
                        bLastPacketSent = true;
                    }
                    baLastPacket.append(Calc8BitCRC(baLastPacket.data(), baLastPacket.length() - nXModemHeaderSize));
                    spSerialPort.write(baLastPacket);

                    ui->progressBar->setValue((nCFilePos*PERCENT_100)/fpFirmwareFile.size());

                    ui->edit_Log->appendPlainText(QString("Sent packet #").append(QString::number(nCPacket)).append(", offset ").append(QString::number(nCFilePos)).append(" of length ").append(QString::number(baLastPacket.length())));
                    ++nCPacket;
                    nCFilePos += nXModemDataSize;
                }
                else
                {
                    //Finished transfer, send end of frame message
                    nAction = ActionModeTypes::ActionModeTypeXModemSendEndOfFrame;
                    baLastPacket.clear();
                    unsigned char cTmp;
                    cTmp = XModemPacketTypes::XModemPacketTypeEndOfFrame;
                    baLastPacket.append(cTmp);
                    spSerialPort.write(baLastPacket);

                    ui->edit_Log->appendPlainText("Sent EOT packet");
                }
            }
            else if (baRecData.at(0) == XModemPacketTypes::XModemPacketTypeNack)
            {
                //XModem NACK
                ui->edit_Log->appendPlainText("Got NACK");
                if (nAction == ActionModeTypes::ActionModeTypeXModemWaitForNack)
                {
                    //First NACK packet has been received, modem is now ready to receive real first packet - the modem has a non-standard XModem implementation and this is a quirk
                    nAction = ActionModeTypes::ActionModeTypeXModemSendData;
                    nCFilePos = 0;
                    nCPacket = XMODEM_FIRST_PACKET_ID;
                    baLastPacket.clear();
                    unsigned char cTmp;
                    cTmp = XModemPacketTypes::XModemPacketType1024BytePacket;
                    baLastPacket.append(cTmp);
                    cTmp = nCPacket;
                    baLastPacket.append(cTmp);
                    cTmp = XMODEM_INVERSE - nCPacket;
                    baLastPacket.append(cTmp);
                    fpFirmwareFile.seek(0);
                    baLastPacket.append(fpFirmwareFile.read(nXModemDataSize));
                    baLastPacket.append(Calc8BitCRC(baLastPacket.data(), nXModemDataSize));
                    spSerialPort.write(baLastPacket);

                    ui->progressBar->setValue((nCFilePos*PERCENT_100)/fpFirmwareFile.size());

                    ui->edit_Log->appendPlainText(QString("Sent packet #").append(QString::number(nCPacket)).append(", offset ").append(QString::number(nCFilePos)).append(" of length ").append(QString::number(baLastPacket.length())));
                    nCFilePos = nXModemDataSize;
                    nCPacket = XMODEM_SECOND_PACKET_ID;
                }
                else if (nAction == ActionModeTypes::ActionModeTypeXModemSendData)
                {
                    //Last packet has an error, retransmit it
                    spSerialPort.write(baLastPacket);
                }
            }
        }
        else
        {
            //Not in main XModem data transfer
            ui->edit_Log->appendPlainText(QString("Got: ").append(baRecData));
            if (nAction == ActionModeTypes::ActionModeTypeXModemSendEndOfFrame)
            {
                //We are finished
                nAction = ActionModeTypes::ActionModeTypeXModemFinished;
                nBytesWritten = 0;
                fpFirmwareFile.close();
                ui->edit_Log->appendPlainText(QString("Sending firmware upgrade accept command..."));

                baLastPacket.clear();
                baLastPacket = QByteArray(baFirmwareUpgradeAcceptCommand).append(baCRLF);
                spSerialPort.write(baLastPacket);
            }
        }
    }
    else if (nAppMode == ApplicationModeTypes::ApplicationModeTypeFirmwareUpdateModeCheck || nAppMode == ApplicationModeTypes::ApplicationModeTypeQuery)
    {
        //Firmware download mode - query which mode
        baRecBuf.append(baRecData);
        if (nAction == ActionModeTypes::ActionModeTypeModem)
        {
            //Checking which mode
            if (baRecBuf.length() > 1 && baRecBuf.at(BOOTLOADER_ERROR_CHAR_INDEX) == BOOTLOADER_ERROR_CHAR && baRecBuf.at(BOOTLOADER_ERROR_RESPONSE_INDEX) == BOOTLOADER_ERROR_UNRECOGNISED)
            {
                //In bootloader
                nAction = ActionModeTypes::ActionModeTypeBootloaderUnbridged;
                ui->edit_Log->appendPlainText("Module in bootloader mode");
                spSerialPort.write(baBootloaderUnlockCommand);
            }
            else
            {
                if (baRecBuf.indexOf(baModemModel) != INDEX_NOT_FOUND)
                {
                    //In modem mode
                    ui->edit_Log->appendPlainText("Module in modem mode");

                    //Extract version
                    QString strFirmwareVersion = baRecBuf.mid(baRecBuf.indexOf(baModemModel)+nModemVersionCutChars, baRecBuf.indexOf(baCR, baRecBuf.indexOf(baModemModel)+baCR.length())-(baRecBuf.indexOf(baModemModel)+nModemVersionCutChars));
                    if (baRecBuf.length() >= MODEM_VERSION_MODEL_MINIMUM_SIZE && strFirmwareVersion.length() >= MODEM_VERSION_MINIMUM_SIZE)
                    {
                        baRecBuf.clear();
                        ui->edit_Log->appendPlainText(QString("Current modem firmware version: ").append(strFirmwareVersion));

                        bool bContinue = true;

                        if (nAppMode == ApplicationModeTypes::ApplicationModeTypeQuery)
                        {
                            //Just checking firmware, display result to user
                            QString strMessage = QString("Modem is running firmware version ").append(strFirmwareVersion);
                            pmErrorForm->SetMessage(&strMessage);
                            pmErrorForm->show();
                            bContinue = false;
                        }
                        else
                        {
                            //Not just checking firmware version on module
                            if (ui->edit_File->text().indexOf(QString(strFirmwareVersion).append(strFileVersionTo)) == INDEX_NOT_FOUND)
                            {
                                //Check if user is sure they want to continue
                                bContinue = (QMessageBox::question(this, "Confirm upgrade", QString("Your module modem appears to be running firmware version ").append(strFirmwareVersion).append(" which might not be compatible with the selected upgrade file ").append((ui->edit_File->text().indexOf(":\\") != INDEX_NOT_FOUND ? ui->edit_File->text().mid(ui->edit_File->text().lastIndexOf("\\")+1) : ui->edit_File->text().mid(ui->edit_File->text().lastIndexOf("/")+1))).append(", do you want to continue?"), QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes);
                            }

                            if (bContinue == true)
                            {
                                //Firmware upgrade mode
                                nAppMode = ApplicationModeTypes::ApplicationModeTypeFirmwareUpdate;
                                fpFirmwareFile.setFileName(ui->edit_File->text());
                                if (fpFirmwareFile.open(QFile::ReadOnly))
                                {
                                    ui->edit_Log->appendPlainText(QString("Opened FOTO file, size: ").append(QString::number(fpFirmwareFile.size())));
                                    nAction = ActionModeTypeXModemWaitForNack;
                                    bLastPacketSent = false;
                                    spSerialPort.write(QByteArray(baFirmwareUpgradeStartCommand).append("=").append(QString::number(fpFirmwareFile.size()).toUtf8()).append(baCRLF));
                                }
                                else
                                {
                                    ui->edit_Log->appendPlainText(QString("Error occured trying to open FOTO file: ").append(fpFirmwareFile.errorString()));
                                    bContinue = false;
                                    QString strMessage = QString("Failed to open FOTO file '").append(ui->edit_File->text()).append("' for reading: ").append(fpFirmwareFile.errorString());
                                    pmErrorForm->SetMessage(&strMessage);
                                    pmErrorForm->show();
                                }
                            }
                        }

                        if (bContinue == false)
                        {
                            spSerialPort.close();
                            SetInputsEnabled(true);
                        }
                    }
                }
                else if (baRecBuf.indexOf(baModemError) != INDEX_NOT_FOUND)
                {
                    //In modem mode, query firmware
                    baRecBuf.clear();
                    ui->edit_Log->appendPlainText("UARTs already bridged, checking modem firmware version...");
                    spSerialPort.write(QByteArray(baVersionQueryCommand).append(baCR));
                }
                else if (baRecBuf.indexOf(baNotFoundError) != INDEX_NOT_FOUND || baRecBuf.length() > ZEPHYR_APPLICATION_TRIGGER_DATA_SIZE)
                {
                    //In Zephyr application
                    baRecBuf.clear();
                    ui->edit_Log->appendPlainText("Module in Zephyr-application mode");
                    spSerialPort.write(baZephyrEnterBootloader);
                    nAction = ActionModeTypes::ActionModeTypeUserApplication;

                    //Start the recurring timer to check if the bootloader has been entered
                    nBootloaderTimerChecks = 0;
                    tmrBootloaderEntranceTimer.start(BOOTLOADER_ENTER_TIMER_CHECK_MS);
                }
            }
        }
        else if (nAction == ActionModeTypes::ActionModeTypeBootloaderUnbridged)
        {
            //Bridge UARTs together to talk to modem
            baRecBuf.clear();
            ui->edit_Log->appendPlainText("Bridging UARTs...");
            spSerialPort.write(baBootloaderBridgeUARTsCommand);
            nAction = ActionModeTypes::ActionModeTypeBootloaderBridged;
        }
        else if (nAction == ActionModeTypes::ActionModeTypeBootloaderBridged)
        {
            if (baRecBuf.length() > MODEM_WAKEUP_RESPONSE_MINIMUM_SIZE)
            {
                //Modem should have started, check
                baRecBuf.clear();
                ui->edit_Log->appendPlainText("Checking modem firmware version...");
                nAction = ActionModeTypes::ActionModeTypeModem;
                spSerialPort.write(QByteArray(baVersionQueryCommand).append(baCR));
            }
        }
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::SerialError(
    QSerialPort::SerialPortError speErrorCode
    )
{
    if (speErrorCode == QSerialPort::NoError)
    {
        //No error, nothing more to do
        return;
    }
    else if (speErrorCode == QSerialPort::ResourceError || speErrorCode == QSerialPort::PermissionError)
    {
        //Serial port error or was not able to open - unable to continue
        QString strMessage = QString("Error occured whilst trying to open or use the serial port, error code: ").append(QString::number(speErrorCode));
        pmErrorForm->SetMessage(&strMessage);
        pmErrorForm->show();
        SetInputsEnabled(true);
        ui->edit_Log->appendPlainText("An error occured whilst trying to open/use the serial port");
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::SerialBytesWritten(
    qint64 intByteCount
    )
{
    if (nAppMode == ApplicationModeTypes::ApplicationModeTypeFirmwareUpdate && nAction == ActionModeTypes::ActionModeTypeXModemFinished)
    {
        nBytesWritten += intByteCount;
        if (nBytesWritten == baLastPacket.length())
        {
            //Upgrade finished
            spSerialPort.close();
            ui->edit_Log->appendPlainText(QString("Finished XModem transfer & serial port closed after ").append(QString::number(etmrElapsed.elapsed()/1000)).append(" seconds. Note that the module may be busy for a few minutes whilst the modem updates itself, this can be monitored using a serial program utility e.g. UwTerminalX, the unit can be safely rebooted once a response is recieved from the module."));
            etmrElapsed.invalidate();
            ui->progressBar->setValue(PERCENT_100);
            SetInputsEnabled(true);
        }
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::OpenSerialPort(
    )
{
    QString strErrorMessage = "";
    bool bErrorOccured = false;
    if (ui->combo_COM->currentText().isEmpty())
    {
        strErrorMessage = "No serial is selected.";
        bErrorOccured = true;
    }
    else
    {
        //Configure serial port object
        spSerialPort.setPortName(ui->combo_COM->currentText());
        spSerialPort.setBaudRate(ui->combo_Baud->currentText().toInt());
        spSerialPort.setDataBits(QSerialPort::Data8);
        spSerialPort.setStopBits(QSerialPort::OneStop);
        spSerialPort.setParity(QSerialPort::NoParity);
        spSerialPort.setFlowControl((ui->combo_Handshake->currentIndex() == ComboBaudRateHandshakingHardware ? QSerialPort::HardwareControl : (ui->combo_Handshake->currentIndex() == ComboBaudRateHandshakingSoftware ? QSerialPort::SoftwareControl : QSerialPort::NoFlowControl)));

        if (spSerialPort.open(QIODevice::ReadWrite))
        {
            //Serial port opened successfully
            etmrElapsed.start();
            ui->edit_Log->appendPlainText("Opened serial port");

            nAction = ActionModeTypes::ActionModeTypeModem;

            //Query device mode
            spSerialPort.write(QByteArray(baVersionQueryCommand).append(baCR));
        }
        else
        {
            //Serial port opening failed
            strErrorMessage = QString("Failed to open serial port '").append(ui->combo_COM->currentText()).append("': ").append(spSerialPort.errorString());
            bErrorOccured = true;
        }
    }

    if (bErrorOccured == true)
    {
        pmErrorForm->SetMessage(&strErrorMessage);
        pmErrorForm->show();
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_Start_clicked(
    )
{
    //Begin the selection action
    QString strErrorMessage = "";
    bool bHasError = false;

    if (ui->combo_COM->currentText().isEmpty())
    {
        //No port selected
        strErrorMessage = "No port has been selected.";
        bHasError = true;
    }
    else if (!ui->radio_LocalFile->isChecked() && !ui->radio_Online->isChecked())
    {
        //Neither local file or download file selected
        strErrorMessage = "Firmware selection by local file or remote download is required.";
        bHasError = true;
    }
    else if (ui->radio_LocalFile->isChecked() && ui->edit_File->text().isEmpty())
    {
        //Local file selected but there is no file
        strErrorMessage = "Local firmware file must be selected.";
        bHasError = true;
    }
    else if (ui->radio_LocalFile->isChecked() && !QFile::exists(ui->edit_File->text()))
    {
        //Local file selected but file does not exist
        strErrorMessage = QString("Local firmware file '").append(ui->edit_File->text()).append("' does not exist.");
        bHasError = true;
    }
    else if (ui->radio_Online->isChecked() && ui->list_Firmwares->selectedItems().count() != 1)
    {
        //Download file but no file is selected
        strErrorMessage = "Remote firmware download selected but no firmware has been selected.";
        bHasError = true;
    }

    if (bHasError == false)
    {
        //Disable inputs
        SetInputsEnabled(false);

        if (ui->radio_Online->isChecked())
        {
            //Download file
            bool bSkipDownload = false;
            nAppMode = ApplicationModeTypes::ApplicationModeTypeOnlineFileDownload;

            std::list<FirmwareListStruct>::iterator it = lstFirmwareFiles.begin();
            uint8_t i = ui->list_Firmwares->row(ui->list_Firmwares->selectedItems().at(0));
            while (i > 0)
            {
                it++;
                --i;
            }

            //Check if file already exists
            QFile fpTestFile(QString(QStandardPaths::writableLocation(QStandardPaths::DataLocation)).append("/").append(it->strFilename));
            if (fpTestFile.exists() && fpTestFile.open(QFile::ReadOnly))
            {
                if (QCryptographicHash::hash(fpTestFile.readAll(), QCryptographicHash::Sha256).toHex() == it->strSHA256)
                {
                    //No need to download file again
                    bSkipDownload = true;
                    ui->edit_File->setText(QString(QStandardPaths::writableLocation(QStandardPaths::DataLocation)).append("/").append(it->strFilename));
                }
                fpTestFile.close();
            }

            if (bSkipDownload == true)
            {
                //Store file in writeable location
                ui->radio_LocalFile->setChecked(true);
                nAppMode = ApplicationModeTypes::ApplicationModeTypeFirmwareUpdateModeCheck;
                OpenSerialPort();
            }
            else
            {
                //Download file
                nmrReply = nmManager->get(QNetworkRequest(QUrl(
#ifdef UseSSL
                    QString((ui->check_SSL->isChecked() ? "https" : "http"))
#else
                    QString("http")
#endif
                    .append("://").append(strOnlineHost).append("/Firmware/Files/").append(it->strFilename))));
            }
        }
        else
        {
            //Use local file
            nAppMode = ApplicationModeTypes::ApplicationModeTypeFirmwareUpdateModeCheck;
            OpenSerialPort();
        }
    }

    if (bHasError == true)
    {
        pmErrorForm->SetMessage(&strErrorMessage);
        pmErrorForm->show();
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_OpenDownloads_clicked(
    )
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DataLocation)));
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_Browse_clicked(
    )
{
    //Browse for a local firmware upgrade file
    QString strFilename = QFileDialog::getOpenFileName(this, "Open File", "", "Firmware files (*.foto;*.ua);;All Files (*.*)");
    if (!strFilename.isEmpty())
    {
        //File was selected
        ui->edit_File->setText(strFilename);
    }
}

//=============================================================================
//=============================================================================
uint8_t
MainWindow::Calc8BitCRC(
    char *pData,
    uint16_t nSize
    )
{
    //Calculates an 8-bit XModem checksum
    uint8_t nCRC = 0;
    uint16_t i = 0;

    //Skip header
    pData += nXModemHeaderSize;

    while (i < nSize)
    {
        nCRC += (uint8_t)*pData;
        ++pData;
        ++i;
    }

    return nCRC;
}

//=============================================================================
//=============================================================================
void
MainWindow::SetInputsEnabled(
    bool bEnabled
    )
{
    ui->btn_Start->setEnabled(bEnabled);
    ui->radio_LocalFile->setEnabled(bEnabled);
    ui->radio_Online->setEnabled(bEnabled);
#ifdef UseSSL
    ui->check_SSL->setEnabled(bEnabled);
#endif
    ui->btn_OnlineFirmwareRefresh->setEnabled(bEnabled);
    ui->btn_Refresh->setEnabled(bEnabled);
    ui->btn_Query->setEnabled(bEnabled);
    ui->combo_COM->setEnabled(bEnabled);
    ui->combo_Baud->setEnabled(bEnabled);
    ui->combo_Handshake->setEnabled(bEnabled);
    ui->edit_File->setEnabled(bEnabled);

    if (bEnabled == true)
    {
        if (ui->radio_LocalFile->isChecked())
        {
            on_radio_LocalFile_toggled(true);
        }
        else if (ui->radio_Online->isChecked())
        {
            on_radio_Online_toggled(true);
        }
    }
    else
    {
        ui->edit_File->setEnabled(false);
        ui->btn_Browse->setEnabled(false);
        ui->btn_OnlineFirmwareRefresh->setEnabled(false);
        ui->list_Firmwares->setEnabled(false);
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::on_combo_COM_currentIndexChanged(
    int
    )
{
    //Serial port selection has been changed, update text
    if (ui->combo_COM->currentText().length() > 0)
    {
        QSerialPortInfo spiSerialInfo(ui->combo_COM->currentText());
        if (spiSerialInfo.isValid())
        {
            //Port exists
            QString strDisplayText(spiSerialInfo.description());
            if (spiSerialInfo.manufacturer().length() > 1)
            {
                //Add manufacturer
                strDisplayText.append(" (").append(spiSerialInfo.manufacturer()).append(")");
            }
            if (spiSerialInfo.serialNumber().length() > 1)
            {
                //Add serial
                strDisplayText.append(" [").append(spiSerialInfo.serialNumber()).append("]");
            }
            ui->label_SerialInfo->setText(strDisplayText);
        }
        else
        {
            //No such port
            ui->label_SerialInfo->setText("Invalid serial port selected");
        }
    }
    else
    {
        //Clear text as no port is selected
        ui->label_SerialInfo->clear();
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_Query_clicked(
    )
{
    //Query the current firmware version on the module
    SetInputsEnabled(false);
    nAppMode = ApplicationModeTypes::ApplicationModeTypeQuery;
    OpenSerialPort();
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_OnlineFirmwareRefresh_clicked(
    )
{
    SetInputsEnabled(false);
    nAppMode = ApplicationModeTypes::ApplicationModeTypeOnlineRefresh;
    nmrReply = nmManager->get(QNetworkRequest(QUrl(
#ifdef UseSSL
        QString((ui->check_SSL->isChecked() ? "https" : "http"))
#else
        QString("http")
#endif
        .append("://").append(strOnlineHost).append("/Firmware/firmware.php?JSON=1&Dev=Pinnacle_100"))));
}

//=============================================================================
//=============================================================================
void
MainWindow::replyFinished(
    QNetworkReply* nrReply
    )
{
    //Response received from online server
    if (nrReply->error() != QNetworkReply::NoError && nrReply->error() != QNetworkReply::ServiceUnavailableError)
    {
        //Display error message if operation wasn't cancelled
        if (nrReply->error() != QNetworkReply::OperationCanceledError)
        {
            //Output error message
            QString strMessage = QString("An error occured during an online request: ").append(nrReply->errorString());
            pmErrorForm->SetMessage(&strMessage);
            pmErrorForm->show();
            SetInputsEnabled(true);
            ui->edit_Log->appendPlainText("Error occured during online request");
        }
    }
    else
    {
        //if (gchTermMode == MODE_CHECK_FIRMWARE_VERSIONS)
        if (nAppMode == ApplicationModeTypes::ApplicationModeTypeOnlineRefresh)
        {
            //Response containing latest firmware versions for modules
            QByteArray baTmpBA = nrReply->readAll();
            QJsonParseError jpeJsonError;
            QJsonDocument jdJsonData = QJsonDocument::fromJson(baTmpBA, &jpeJsonError);

            if (jpeJsonError.error == QJsonParseError::NoError)
            {
                //Decoded JSON
                QJsonObject joJsonObject = jdJsonData.object();

                if (joJsonObject["Result"].toString() == strOnlineResponseValid)
                {
                    //Update version list
                    FirmwareListStruct strNewFirmware;
                    ui->list_Firmwares->clear();
                    lstFirmwareFiles.clear();

                    QJsonArray joJsonFirmwareObjects = joJsonObject["Devices"].toObject()["Pinnacle_100"].toArray();
                    uint8_t i = 0;
                    while (i < joJsonFirmwareObjects.count())
                    {
                        QJsonArray joJsonFirmwareObject = joJsonFirmwareObjects.at(i).toArray();
                        strNewFirmware.strFilename = joJsonFirmwareObject.at(OnlineFirmwareJSONIndexFilename).toString();
                        strNewFirmware.strFromVersion = joJsonFirmwareObject.at(OnlineFirmwareJSONIndexFromVersion).toString();
                        strNewFirmware.strToVersion = joJsonFirmwareObject.at(OnlineFirmwareJSONIndexToVersion).toString();
                        strNewFirmware.strSHA256 = joJsonFirmwareObject.at(OnlineFirmwareJSONIndexSHA256).toString();
                        lstFirmwareFiles.push_back(strNewFirmware);
                        ui->list_Firmwares->addItem(QString(strNewFirmware.strFromVersion).append(" to ").append(strNewFirmware.strToVersion));
                        ++i;
                    }
                }
                else
                {
                    //Server responded with error
                    QString strMessage = QString("Server responded with error code ").append(joJsonObject["Result"].toString()).append(": ").append(joJsonObject["Error"].toString());
                    pmErrorForm->SetMessage(&strMessage);
                    pmErrorForm->show();
                    ui->edit_Log->appendPlainText(QString("Error occured with online request (error: ").append(joJsonObject["Error"].toString()).append(")"));
                }
            }
            else
            {
                //Error whilst decoding JSON
                QString strMessage = QString("Unable to decode JSON data from server, debug data: ").append(jdJsonData.toBinaryData());
                pmErrorForm->SetMessage(&strMessage);
                pmErrorForm->show();
                ui->edit_Log->appendPlainText("Error occured with decoding online JSON data");
            }
            SetInputsEnabled(true);
        }
        else if (nAppMode == ApplicationModeTypes::ApplicationModeTypeOnlineFileDownload)
        {
            //Firmware upgrade file data received from server
            std::list<FirmwareListStruct>::iterator it = lstFirmwareFiles.begin();
            uint8_t i = ui->list_Firmwares->row(ui->list_Firmwares->selectedItems().first());
            while (i > 0)
            {
                it++;
                --i;
            }

            //Store file in writeable location
            QFile fpTestFile(QString(QStandardPaths::writableLocation(QStandardPaths::DataLocation)).append("/").append(it->strFilename));
            ui->edit_File->setText(QString(QStandardPaths::writableLocation(QStandardPaths::DataLocation)).append("/").append(it->strFilename));
            fpTestFile.open(QFile::ReadWrite | QFile::Truncate);
            fpTestFile.write(nrReply->readAll());
            fpTestFile.flush();
            fpTestFile.close();

            //Switch to local file firmware download and begin the update process
            ui->radio_LocalFile->setChecked(true);
            nAppMode = ApplicationModeTypes::ApplicationModeTypeFirmwareUpdateModeCheck;
            OpenSerialPort();
        }
    }

    //Queue the network reply object to be deleted
    nrReply->deleteLater();
}

//=============================================================================
//=============================================================================
#ifdef UseSSL
void
MainWindow::sslErrors(
    QNetworkReply* nrReply,
    QList<QSslError> lstSSLErrors
    )
{
    //Error detected with SSL
    if (sslcLairdConnectivity != NULL && nrReply->sslConfiguration().peerCertificate() == *sslcLairdConnectivity)
    {
        //Server certificate matches
        nrReply->ignoreSslErrors(lstSSLErrors);
    }
}
#endif

//=============================================================================
//=============================================================================
void
MainWindow::BootloaderEntranceTimerTimeout(
    )
{
    //Should now be in bootloader mode
    if (spSerialPort.pinoutSignals() & QSerialPort::ClearToSendSignal)
    {
        //CTS is asserted, we are in the bootloader
        tmrBootloaderEntranceTimer.stop();
        nAction = ActionModeTypes::ActionModeTypeBootloaderUnbridged;
        ui->edit_Log->appendPlainText("Module in bootloader mode (assumed)");
        spSerialPort.write(baBootloaderUnlockCommand);
        return;
    }

    ++nBootloaderTimerChecks;
    if (nBootloaderTimerChecks > BOOTLOADER_ENTER_CHECK_TIMES)
    {
        //Failed to enter bootloader mode
        tmrBootloaderEntranceTimer.stop();
        QString strMessage = "CTS is de-asserted, module has failed to enter bootloader mode.";
        pmErrorForm->SetMessage(&strMessage);
        pmErrorForm->show();
        ui->edit_Log->appendPlainText("Error occured with module entering bootloader mode (CTS de-asserted)");
        spSerialPort.close();
        SetInputsEnabled(true);
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_Refresh_clicked(
    )
{
    RefreshSerialDevices();
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_ClearLog_clicked(
    )
{
    ui->edit_Log->clear();
}

//=============================================================================
//=============================================================================
void
MainWindow::on_radio_LocalFile_toggled(
    bool bChecked
    )
{
    if (bChecked == true)
    {
        ui->edit_File->setEnabled(true);
        ui->btn_Browse->setEnabled(true);
        ui->btn_OnlineFirmwareRefresh->setEnabled(false);
        ui->list_Firmwares->setEnabled(false);
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::on_radio_Online_toggled(
    bool bChecked
    )
{
    if (bChecked == true)
    {
        ui->edit_File->setEnabled(false);
        ui->btn_Browse->setEnabled(false);
        ui->btn_OnlineFirmwareRefresh->setEnabled(true);
        ui->list_Firmwares->setEnabled(true);
    }
}

//=============================================================================
//=============================================================================
void
MainWindow::on_btn_Licenses_clicked(
    )
{
    //Show license text
    QString strMessage = tr("XModemUtil uses the Qt framework version 5, which is licensed under the GPLv3 (not including later versions).\nXModemUtil uses and may be linked statically to various other libraries including Xau, XCB, expat, fontconfig, zlib, bz2, harfbuzz, freetype, udev, dbus, icu, unicode, UPX, OpenSSL. The licenses for these libraries are provided below:\n\n\n\nLib Xau:\n\nCopyright 1988, 1993, 1994, 1998  The Open Group\n\nPermission to use, copy, modify, distribute, and sell this software and its\ndocumentation for any purpose is hereby granted without fee, provided that\nthe above copyright notice appear in all copies and that both that\ncopyright notice and this permission notice appear in supporting\ndocumentation.\nThe above copyright notice and this permission notice shall be included in\nall copies or substantial portions of the Software.\nTHE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\nIMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\nFITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE\nOPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN\nAN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN\nCONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\nExcept as contained in this notice, the name of The Open Group shall not be\nused in advertising or otherwise to promote the sale, use or other dealings\nin this Software without prior written authorization from The Open Group.\n\n\n\nxcb:\n\nCopyright (C) 2001-2006 Bart Massey, Jamey Sharp, and Josh Triplett.\nAll Rights Reserved.\n\nPermission is hereby granted, free of charge, to any person\nobtaining a copy of this software and associated\ndocumentation files (the 'Software'), to deal in the\nSoftware without restriction, including without limitation\nthe rights to use, copy, modify, merge, publish, distribute,\nsublicense, and/or sell copies of the Software, and to\npermit persons to whom the Software is furnished to do so,\nsubject to the following conditions:\n\nThe above copyright notice and this permission notice shall\nbe included in all copies or substantial portions of the\nSoftware.\n\nTHE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY\nKIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE\nWARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR\nPURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS\nBE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER\nIN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\nOUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR\nOTHER DEALINGS IN THE SOFTWARE.\n\nExcept as contained in this notice, the names of the authors\nor their institutions shall not be used in advertising or\notherwise to promote the sale, use or other dealings in this\nSoftware without prior written authorization from the\nauthors.\n\n\n\nexpat:\n\nCopyright (c) 1998, 1999, 2000 Thai Open Source Software Center Ltd\n   and Clark Cooper\nCopyright (c) 2001, 2002, 2003, 2004, 2005, 2006 Expat maintainers.\nPermission is hereby granted, free of charge, to any person obtaining\na copy of this software and associated documentation files (the\n'Software'), to deal in the Software without restriction, including\nwithout limitation the rights to use, copy, modify, merge, publish,\ndistribute, sublicense, and/or sell copies of the Software, and to\npermit persons to whom the Software is furnished to do so, subject to\nthe following conditions:\n\nThe above copyright notice and this permission notice shall be included\nin all copies or substantial portions of the Software.\n\nTHE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,\nEXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF\nMERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\nIN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY\nCLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,\nTORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE\nSOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n\n\nfontconfig:\n\nCopyright © 2001,2003 Keith Packard\n\nPermission to use, copy, modify, distribute, and sell this software and its\ndocumentation for any purpose is hereby granted without fee, provided that\nthe above copyright notice appear in all copies and that both that\ncopyright notice and this permission notice appear in supporting\ndocumentation, and that the name of Keith Packard not be used in\nadvertising or publicity pertaining to distribution of the software without\nspecific, written prior permission.  Keith Packard makes no\nrepresentations about the suitability of this software for any purpose.  It\nis provided 'as is' without express or implied warranty.\n\nKEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,\nINCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO\nEVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR\nCONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,\nDATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER\nTORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR\nPERFORMANCE OF THIS SOFTWARE.\n\nz:\n\n (C) 1995-2013 Jean-loup Gailly and Mark Adler\n\n  This software is provided 'as-is', without any express or implied\n  warranty.  In no event will the authors be held liable for any damages\n  arising from the use of this software.\n\n  Permission is granted to anyone to use this software for any purpose,\n  including commercial applications, and to alter it and redistribute it\n  freely, subject to the following restrictions:\n\n  1. The origin of this software must not be misrepresented; you must not\n     claim that you wrote the original software. If you use this software\n     in a product, an acknowledgment in the product documentation would be\n     appreciated but is not required.\n  2. Altered source versions must be plainly marked as such, and must not be\n     misrepresented as being the original software.\n  3. This notice may not be removed or altered from any source distribution.\n\n  Jean-loup Gailly        Mark Adler\n  jloup@gzip.org          madler@alumni.caltech.edu\n\n\n\nbz2:\n\n\nThis program, 'bzip2', the associated library 'libbzip2', and all\ndocumentation, are copyright (C) 1996-2010 Julian R Seward.  All\nrights reserved.\n\nRedistribution and use in source and binary forms, with or without\nmodification, are permitted provided that the following conditions\nare met:\n\n1. Redistributions of source code must retain the above copyright\n   notice, this list of conditions and the following disclaimer.\n\n2. The origin of this software must not be misrepresented; you must\n   not claim that you wrote the original software.  If you use this\n   software in a product, an acknowledgment in the product\n   documentation would be appreciated but is not required.\n\n3. Altered source versions must be plainly marked as such, and must\n   not be misrepresented as being the original software.\n\n4. The name of the author may not be used to endorse or promote\n   products derived from this software without specific prior written\n   permission.\n\nTHIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS\nOR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\nWARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\nARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY\nDIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\nDAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE\nGOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\nINTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,\nWHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING\nNEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\nSOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n\nJulian Seward, jseward@bzip.org\nbzip2/libbzip2 version 1.0.6 of 6 September 2010\n\n\n\nharfbuzz:\n\nHarfBuzz is licensed under the so-called 'Old MIT' license.  Details follow.\n\nCopyright © 2010,2011,2012  Google, Inc.\nCopyright © 2012  Mozilla Foundation\nCopyright © 2011  Codethink Limited\nCopyright © 2008,2010  Nokia Corporation and/or its subsidiary(-ies)\nCopyright © 2009  Keith Stribley\nCopyright © 2009  Martin Hosken and SIL International\nCopyright © 2007  Chris Wilson\nCopyright © 2006  Behdad Esfahbod\nCopyright © 2005  David Turner\nCopyright © 2004,2007,2008,2009,2010  Red Hat, Inc.\nCopyright © 1998-2004  David Turner and Werner Lemberg\n\nFor full copyright notices consult the individual files in the package.\n\nPermission is hereby granted, without written agreement and without\nlicense or royalty fees, to use, copy, modify, and distribute this\nsoftware and its documentation for any purpose, provided that the\nabove copyright notice and the").append(" following two paragraphs appear in\nall copies of this software.\n\nIN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR\nDIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES\nARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN\nIF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH\nDAMAGE.\n\nTHE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,\nBUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND\nFITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS\nON AN 'AS IS' BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO\nPROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.\n\n\n\nfreetype:\n\nThe  FreeType 2  font  engine is  copyrighted  work and  cannot be  used\nlegally  without a  software license.   In  order to  make this  project\nusable  to a vast  majority of  developers, we  distribute it  under two\nmutually exclusive open-source licenses.\n\nThis means  that *you* must choose  *one* of the  two licenses described\nbelow, then obey  all its terms and conditions when  using FreeType 2 in\nany of your projects or products.\n\n  - The FreeType License, found in  the file `FTL.TXT', which is similar\n    to the original BSD license *with* an advertising clause that forces\n    you  to  explicitly cite  the  FreeType  project  in your  product's\n    documentation.  All  details are in the license  file.  This license\n    is  suited  to products  which  don't  use  the GNU  General  Public\n    License.\n\n    Note that  this license  is  compatible  to the  GNU General  Public\n    License version 3, but not version 2.\n\n  - The GNU General Public License version 2, found in  `GPLv2.TXT' (any\n    later version can be used  also), for programs which already use the\n    GPL.  Note  that the  FTL is  incompatible  with  GPLv2 due  to  its\n    advertisement clause.\n\nThe contributed BDF and PCF drivers come with a license similar  to that\nof the X Window System.  It is compatible to the above two licenses (see\nfile src/bdf/README and src/pcf/README).\n\nThe gzip module uses the zlib license (see src/gzip/zlib.h) which too is\ncompatible to the above two licenses.\n\nThe MD5 checksum support (only used for debugging in development builds)\nis in the public domain.\n\n\n\nudev:\n\nCopyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>\nCopyright (C) 2003-2010 Kay Sievers <kay@vrfy.org>\n\nThis program is free software: you can redistribute it and/or modify\nit under the terms of the GNU General Public License as published by\nthe Free Software Foundation, either version 2 of the License, or\n(at your option) any later version.\n\nThis program is distributed in the hope that it will be useful,\nbut WITHOUT ANY WARRANTY; without even the implied warranty of\nMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\nGNU General Public License for more details.\n\nYou should have received a copy of the GNU General Public License\nalong with this program.  If not, see <http://www.gnu.org/licenses/>.\n\n\n\ndbus:\n\nD-Bus is licensed to you under your choice of the Academic Free\nLicense version 2.1, or the GNU General Public License version 2\n(or, at your option any later version).\n\n\n\nicu:\n\nICU License - ICU 1.8.1 and later\nCOPYRIGHT AND PERMISSION NOTICE\nCopyright (c) 1995-2015 International Business Machines Corporation and others\nAll rights reserved.\nPermission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the 'Software'), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, provided that the above copyright notice(s) and this permission notice appear in all copies of the Software and that both the above copyright notice(s) and this permission notice appear in supporting documentation.\nTHE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.\nExcept as contained in this notice, the name of a copyright holder shall not be used in advertising or otherwise to promote the sale, use or other dealings in this Software without prior written authorization of the copyright holder.\n\n\n\nUnicode:\n\nCOPYRIGHT AND PERMISSION NOTICE\n\nCopyright © 1991-2015 Unicode, Inc. All rights reserved.\nDistributed under the Terms of Use in\nhttp://www.unicode.org/copyright.html.\n\nPermission is hereby granted, free of charge, to any person obtaining\na copy of the Unicode data files and any associated documentation\n(the 'Data Files') or Unicode software and any associated documentation\n(the 'Software') to deal in the Data Files or Software\nwithout restriction, including without limitation the rights to use,\ncopy, modify, merge, publish, distribute, and/or sell copies of\nthe Data Files or Software, and to permit persons to whom the Data Files\nor Software are furnished to do so, provided that\n(a) this copyright and permission notice appear with all copies\nof the Data Files or Software,\n(b) this copyright and permission notice appear in associated\ndocumentation, and\n(c) there is clear notice in each modified Data File or in the Software\nas well as in the documentation associated with the Data File(s) or\nSoftware that the data or software has been modified.\n\nTHE DATA FILES AND SOFTWARE ARE PROVIDED 'AS IS', WITHOUT WARRANTY OF\nANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE\nWARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\nNONINFRINGEMENT OF THIRD PARTY RIGHTS.\nIN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS\nNOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL\nDAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,\nDATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER\nTORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR\nPERFORMANCE OF THE DATA FILES OR SOFTWARE.\n\nExcept as contained in this notice, the name of a copyright holder\nshall not be used in advertising or otherwise to promote the sale,\nuse or other dealings in these Data Files or Software without prior\nwritten authorization of the copyright holder.\n\n\nUPX:\n\nCopyright (C) 1996-2013 Markus Franz Xaver Johannes Oberhumer\nCopyright (C) 1996-2013 László Molnár\nCopyright (C) 2000-2013 John F. Reiser\n\nAll Rights Reserved. This program may be used freely, and you are welcome to redistribute and/or modify it under certain conditions.\n\nThis program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the UPX License Agreement for more details: http://upx.sourceforge.net/upx-license.html\r\n\r\n\r\nOpenSSL:\r\n\r\nCopyright (c) 1998-2016 The OpenSSL Project.  All rights reserved.\r\n\r\nRedistribution and use in source and binary forms, with or without\r\nmodification, are permitted provided that the following conditions\r\nare met:\r\n\r\n1. Redistributions of source code must retain the above copyright\r\n   notice, this list of conditions and the following disclaimer. \r\n\r\n2. Redistributions in binary form must reproduce the above copyright\r\n   notice, this list of conditions and the following disclaimer in\r\n   the documentation and/or other materials provided with the\r\n   distribution.\r\n\r\n3. All advertising materials mentioning features or use of this\r\n   software must display the following acknowledgment:\r\n   'This product includes software developed by the OpenSSL Project\r\n   for use in the OpenSSL Toolkit. (http://www.openssl.org/)'\r\n\r\n4. The names 'OpenSSL Toolkit' and 'OpenSSL Project' must not be used to\r\n   endorse or promote products derived from this software without\r\n   prior written permission. For written permission, please contact\r\n   openssl-core@openssl.org.\r\n\r\n5. Products derived from this software may not be called 'OpenSSL'\r\n   nor may 'OpenSSL' appear in their names without prior written\r\n   permission of the OpenSSL Project.\r\n\r\n6. Redistributions of any form whatsoever must retain the following\r\n   acknowledgment:\r\n   'This product includes software developed by the OpenSSL Project\r\n   for use in the OpenSSL Toolkit (http://www.openssl.org/)'\r\n\r\nTHIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY\r\nEXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\r\nIMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR\r\nPURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR\r\nITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\r\nSPECIAL, EXEMPLARY, OR").append(" CONSEQUENTIAL DAMAGES (INCLUDING, BUT\r\nNOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;\r\nLOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)\r\nHOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,\r\nSTRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)\r\nARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED\r\nOF THE POSSIBILITY OF SUCH DAMAGE.\r\n====================================================================\r\n\r\nThis product includes cryptographic software written by Eric Young\r\n(eay@cryptsoft.com).  This product includes software written by Tim\r\nHudson (tjh@cryptsoft.com).\r\n\r\n\r\n Original SSLeay License\r\n -----------------------\r\n\r\nCopyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)\r\nAll rights reserved.\r\n\r\nThis package is an SSL implementation written\r\nby Eric Young (eay@cryptsoft.com).\r\nThe implementation was written so as to conform with Netscapes SSL.\r\n\r\nThis library is free for commercial and non-commercial use as long as\r\nthe following conditions are aheared to.  The following conditions\r\napply to all code found in this distribution, be it the RC4, RSA,\r\nlhash, DES, etc., code; not just the SSL code.  The SSL documentation\r\nincluded with this distribution is covered by the same copyright terms\r\nexcept that the holder is Tim Hudson (tjh@cryptsoft.com).\r\n\r\nCopyright remains Eric Young's, and as such any Copyright notices in\r\nthe code are not to be removed.\r\nIf this package is used in a product, Eric Young should be given attribution\r\nas the author of the parts of the library used.\r\nThis can be in the form of a textual message at program startup or\r\nin documentation (online or textual) provided with the package.\r\n\r\nRedistribution and use in source and binary forms, with or without\r\nmodification, are permitted provided that the following conditions\r\nare met:\r\n1. Redistributions of source code must retain the copyright\r\n   notice, this list of conditions and the following disclaimer.\r\n2. Redistributions in binary form must reproduce the above copyright\r\n   notice, this list of conditions and the following disclaimer in the\r\n   documentation and/or other materials provided with the distribution.\r\n3. All advertising materials mentioning features or use of this software\r\n   must display the following acknowledgement:\r\n   'This product includes cryptographic software written by\r\n    Eric Young (eay@cryptsoft.com)'\r\n   The word 'cryptographic' can be left out if the rouines from the library\r\n   being used are not cryptographic related :-).\r\n4. If you include any Windows specific code (or a derivative thereof) from \r\n   the apps directory (application code) you must include an acknowledgement:\r\n   'This product includes software written by Tim Hudson (tjh@cryptsoft.com)'\r\n\r\nTHIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND\r\nANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\r\nIMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE\r\nARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE\r\nFOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\r\nDAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS\r\nOR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)\r\nHOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT\r\nLIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY\r\nOUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF\r\nSUCH DAMAGE.\r\n\r\nThe licence and distribution terms for any publically available version or\r\nderivative of this code cannot be changed.  i.e. this code cannot simply be\r\ncopied and put under another distribution licence\r\n[including the GNU Public Licence.]");
    pmErrorForm->show();
    pmErrorForm->SetMessage(&strMessage);
}

/******************************************************************************/
// END OF FILE
/******************************************************************************/

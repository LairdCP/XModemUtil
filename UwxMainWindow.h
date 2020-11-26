/******************************************************************************
** Copyright (C) 2020 Laird Connectivity
**
** Project: XModemUtil
**
** Module: MainWindow.h
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
#ifndef MainWindow_H
#define MainWindow_H

/******************************************************************************/
// Include Files
/******************************************************************************/
#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QFile>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QListWidgetItem>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include "UwxPopup.h"

/******************************************************************************/
// Defines
/******************************************************************************/
#define XMODEM_INVERSE                            0xff
#define XMODEM_FIRST_PACKET_ID                    1
#define XMODEM_SECOND_PACKET_ID                   2
#define PERCENT_100                               100
#define INDEX_NOT_FOUND                           -1
#define REGEX_SERIAL_INDEX_PORT                   2
#define BOOTLOADER_ERROR_CHAR                     'f'
#define BOOTLOADER_ERROR_UNRECOGNISED             0x04
#define BOOTLOADER_ERROR_CHAR_INDEX               0
#define BOOTLOADER_ERROR_RESPONSE_INDEX           1
#define BOOTLOADER_ENTER_TIMER_CHECK_MS           1500
#define BOOTLOADER_ENTER_CHECK_TIMES              5
#define MODEM_WAKEUP_RESPONSE_MINIMUM_SIZE        3
#define MODEM_VERSION_MODEL_MINIMUM_SIZE          14
#define MODEM_VERSION_MINIMUM_SIZE                7
#define ZEPHYR_APPLICATION_TRIGGER_DATA_SIZE      30

#ifndef QT_NO_SSL
    #define UseSSL //By default enable SSL if Qt supports it (requires OpenSSL runtime libraries). Comment this line out to build without SSL support or if you get errors when communicating with the server
#endif
#ifdef UseSSL
    #include <QSslSocket>
#endif

/******************************************************************************/
// Constants
/******************************************************************************/
const QString    strUtilVersion                 = "0.3"; //Version string
const uint8_t    nXModemPaddingCharacter        = 26;
const qint16     nXModemDataSize                = 1024;
const qint16     nXModemHeaderSize              = 3;
const QByteArray baBootloaderUnlockCommand      = QByteArray("p\x0f\x51\x2a\x51");
const QByteArray baBootloaderBridgeUARTsCommand = QByteArray("~\x01\x06\x01\x06");
const QByteArray baFirmwareUpgradeStartCommand  = QByteArray("AT+WDSD");
const QByteArray baFirmwareUpgradeAcceptCommand = QByteArray("AT+WDSR=4");
const QByteArray baVersionQueryCommand          = QByteArray("ATI3");
const QByteArray baModemError                   = QByteArray("\r\nERROR\r\n");
const QByteArray baCR                           = QByteArray("\r");
const QByteArray baCRLF                         = QByteArray("\r\n");
const QByteArray baModemModel                   = QByteArray("HL7800");
const QByteArray baNotFoundError                = QByteArray("not found");
const uint8_t    nModemVersionCutChars          = 7;
const QString    strFileVersionTo               = QString("_to");
const QByteArray baZephyrEnterBootloader        = QByteArray("mg100 bootloader\r\noob bootloader\r\n");
const QString    strOnlineResponseValid         = QString("1");
const QString    strOnlineHost                  = "uwterminalx.lairdconnect.com";

/******************************************************************************/
// Forward declaration of Class, Struct & Unions
/******************************************************************************/
namespace Ui
{
    class MainWindow;
}

//
typedef struct
{
    QString strFilename;
    QString strFromVersion;
    QString strToVersion;
    QString strSHA256;
} FirmwareListStruct;

//Enum used for type of XModem packet
enum XModemPacketTypes
{
    XModemPacketType128BytePacket               = 0x01,
    XModemPacketType1024BytePacket              = 0x02,
    XModemPacketTypeEndOfFrame                  = 0x04,
    XModemPacketTypeAck                         = 0x06,
    XModemPacketTypeNack                        = 0x15
};

//Enum used for the current application mode
enum ApplicationModeTypes
{
    ApplicationModeTypeFirmwareUpdate            = 0,
    ApplicationModeTypeQuery,
    ApplicationModeTypeOnlineFileDownload,
    ApplicationModeTypeOnlineRefresh,
    ApplicationModeTypeFirmwareUpdateModeCheck
};

//Enum used for the current action type
enum ActionModeTypes
{
    ActionModeTypeModem                         = 0,
    ActionModeTypeBootloaderUnbridged,
    ActionModeTypeBootloaderBridged,
    ActionModeTypeUserApplication,

    ActionModeTypeXModemWaitForNack,
    ActionModeTypeXModemSendData,
    ActionModeTypeXModemSendEndOfFrame,
    ActionModeTypeXModemFinished
};

enum ComboBaudRateIndexes
{
    ComboBaudRateIndex1200                      = 0,
    ComboBaudRateIndex2400,
    ComboBaudRateIndex4800,
    ComboBaudRateIndex9600,
    ComboBaudRateIndex14400,
    ComboBaudRateIndex19200,
    ComboBaudRateIndex38400,
    ComboBaudRateIndex57600,
    ComboBaudRateIndex115200,
    ComboBaudRateIndex230400,
    ComboBaudRateIndex460800,
    ComboBaudRateIndex921600,
    ComboBaudRateIndex1000000
};

enum ComboBaudRateHandshaking
{
    ComboBaudRateHandshakingNone                = 0,
    ComboBaudRateHandshakingHardware,
    ComboBaudRateHandshakingSoftware
};

enum OnlineFirmwareJSONIndexes
{
    OnlineFirmwareJSONIndexFilename             = 0,
    OnlineFirmwareJSONIndexFromVersion,
    OnlineFirmwareJSONIndexToVersion,
    OnlineFirmwareJSONIndexSHA256
};

/******************************************************************************/
// Class definitions
/******************************************************************************/
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit
    MainWindow(
        QWidget *parent = 0
        );
    ~MainWindow(
        );

public slots:
    void
    SerialRead(
        );
    void
    SerialError(
        QSerialPort::SerialPortError speErrorCode
        );
    void
    SerialBytesWritten(
        qint64 intByteCount
        );

private slots:
    void
    on_btn_Start_clicked(
        );
    void
    on_btn_OpenDownloads_clicked(
        );
    void
    on_btn_Browse_clicked(
        );
    void
    on_combo_COM_currentIndexChanged(
        int index
        );
    void
    on_btn_Query_clicked(
        );
    void
    replyFinished(
        QNetworkReply* nrReply
        );
#ifdef UseSSL
    void
    sslErrors(
        QNetworkReply*,
        QList<QSslError>
        );
#endif
    void
    on_btn_OnlineFirmwareRefresh_clicked(
        );
    void
    BootloaderEntranceTimerTimeout(
        );
    void
    on_btn_Refresh_clicked(
        );
    void
    on_btn_ClearLog_clicked(
        );
    void
    on_radio_LocalFile_toggled(
        bool bChecked
        );
    void
    on_radio_Online_toggled(
        bool bChecked
        );
    void
    on_btn_Licenses_clicked(
        );

private:
    void
    RefreshSerialDevices(
        );
    void
    OpenSerialPort(
        );
    uint8_t
    Calc8BitCRC(
        char *pData,
        uint16_t nSize
        );
    void
    SetInputsEnabled(
        bool bEnabled
        );

    Ui::MainWindow *ui;
    QSerialPort spSerialPort;                       //Contains the handle for the serial port
    QFile fpFirmwareFile;                           //Currently open firmware upgrade file
    QByteArray baLastPacket;                        //Contains the last sent (serial) packet
    ApplicationModeTypes nAppMode;                  //Current application mode
    ActionModeTypes nAction;                        //Current action of mode
    uint8_t nCPacket = 0;                           //Current packet index
    uint32_t nCFilePos = 0;                         //Current offset of firmware file for reading
    bool bLastPacketSent = false;                   //If the final end of frame packet has been sent
    QElapsedTimer etmrElapsed;                      //Elapsed timer for timing firmware update
    QTimer tmrBootloaderEntranceTimer;              //Timer used for checking if the bootloader has been entered
    uint16_t nBytesWritten = 0;                     //Bytes written to the remote (serial) device
    QNetworkAccessManager *nmManager = NULL;        //Network access manager
    QNetworkReply *nmrReply = NULL;                 //Network reply
    std::list<FirmwareListStruct> lstFirmwareFiles; //List of remote server firmware upgrade files
    PopupMessage *pmErrorForm = NULL;               //Error message form
    uint8_t nBootloaderTimerChecks = 0;             //Number of times the bootloader status has been checked (timeout checking)
#ifdef UseSSL
    QSslCertificate *sslcLairdConnectivity = NULL;  //Holds the Laird Connectivity SSL certificate
#endif
    QByteArray baRecBuf;                            //Receive buffer (serial)
};

#endif // MainWindow_H

/******************************************************************************/
// END OF FILE
/******************************************************************************/

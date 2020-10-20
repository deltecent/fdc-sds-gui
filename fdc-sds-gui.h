#ifndef FDCSDSGUI_H
#define FDCSDSGUI_H

#include <QDialog>
#include <QTimer>
#include <QFile>
#include <QLabel>
#include <QLCDNumber>
#include <QProgressBar>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QPixmap>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QList>

#define MAX_DRIVE		4
#define CMD_LEN			8                       // does not include checksum bytes
#define CRC_LEN			2			// length of CRC
#define CMDBUF_SIZE		CMD_LEN+CRC_LEN
#define TRKBUF_SIZE		137*32                  // maximum valid track length

#define STAT_OK			0x0000			// OK
#define STAT_NOT_READY		0x0001			// Not Ready
#define STAT_CHECKSUM_ERR	0x0002			// Checksum Error
#define STAT_WRITE_ERR		0x0003			// Write Error

#define DASHBOARD_ROWS		4			// Number of dashboard rows
#define DASHBOARD_STAT		0
#define DASHBOARD_READ		1
#define	DASHBOARD_WRIT		2
#define DASHBOARD_ERR		3			// Error row
#define DASHBOARD_ERRTO		1000			// Error text timeout 10ms ticks

typedef struct TCOMMAND {
	union {
		quint8 asBytes[CMDBUF_SIZE];
		struct {
			char command[4];
			union {
				quint16 param1;
				quint16 rcode;
			};
			union {
				quint16 param2;
				quint16 rdata;
			};
			quint16 checksum;
		};
	};
} tcommand_t;


class DbgWidget : public QTextEdit
{
	Q_OBJECT

public:
	DbgWidget(QWidget *parent = nullptr);
	void hexDump(const quint8 *buffer, int len);
};

class FDCDialog : public QDialog
{
	Q_OBJECT

public:
	FDCDialog(QWidget *parent = 0);

private slots:
	void serialPortSlot(int index);
	void baudRateSlot(int index);
	void loadButtonSlot(int drive);
	void unloadButtonSlot(int drive);
	void timerSlot();

private:
	QTimer *timer;
	QComboBox *serialPortBox;
	QComboBox *baudRateBox;
	QLabel *label;
	QProgressBar *trackProgress[MAX_DRIVE];
	QLineEdit *fileName[MAX_DRIVE];
	QPushButton *loadButton[MAX_DRIVE];
	QPushButton *unloadButton[MAX_DRIVE];
	QList<QSerialPortInfo> serialPorts;
	QSerialPort *serialPort;
	quint32 baudRate;
	tcommand_t cmdBuf;
	quint16 trkBufIdx;
	quint8 trkBuf[TRKBUF_SIZE + CRC_LEN];
	quint8 driveSize[MAX_DRIVE];
	quint16 maxTrack[MAX_DRIVE];
	quint16 curTrack[MAX_DRIVE];
	quint8 headStatus[MAX_DRIVE];
	quint8 enableStatus[MAX_DRIVE];
	QIODevice::OpenMode openMode[MAX_DRIVE];
	QFile *driveFile[MAX_DRIVE];
	QString savePath;
	QLabel *enabledLabel[MAX_DRIVE];
	QLabel *headloadLabel[MAX_DRIVE];
	const QPixmap *grnLED;
	const QPixmap *redLED;
	QTextEdit *debugWindow;
	QLabel *dashboardLabel[DASHBOARD_ROWS];
	quint32 tickCount;
	quint32 statCount;
	quint32 readCount;
	quint32 writCount;
	quint32 errCount;
	quint32 rbyteCount;
	quint32 wbyteCount;
	quint32 errTimeout;
	DbgWidget *dbgWindow;

	void enableDrive(quint8 driveNum);
	void enableHead(quint8 driveNum);
	void updateIndicators(void);
	void updateSerialPort(void);
	int readSerialPort(const quint8 *buffer, int len, qint64 msec=1000);
	int writeSerialPort(const quint8 *buffer, int len, qint64 msec=1000);
	quint16 calcChecksum(const quint8 *data, int length);
	void displayDash(QString text, int row, int pos, int len);
	void displayError(QString text);
	void clearError(void);
	void reject(void);
};
#endif


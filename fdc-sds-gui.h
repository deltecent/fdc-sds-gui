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
#include <QComboBox>
#include <QPixmap>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QList>

#define MAX_DRIVE		4
#define CMDBUF_SIZE		10
#define COMMAND_LENGTH		8                       // does not include checksum bytes
#define TRACKBUF_LEN		137*32                  // maximum valid track length
#define TRACKBUF_LEN_CRC	(TRACKBUF_LEN+2)        // maximum valid track length with CRC
#define STAT_OK			0x0000			// OK
#define STAT_NOT_READY		0x0001			// Not Ready
#define STAT_CHECKSUM_ERR	0x0002			// Checksum Error
#define STAT_WRITE_ERR		0x0003			// Write Error
#define FORM_HEIGHT		575                     // used to keep form a fixed size
#define FORM_WIDTH		512                     // used to keep form a fixed size
#define FORM_WIDTH_DEBUG	727			// width when debug log enabled
#define MAX_FILENAME		78			// max filename length before truncate
#define FILENAME_CHUNK		36                      // chunk of filename to take when splitting

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
	quint16 cmdBufIdx;
	tcommand_t cmdBuf;
	quint16 trkBufIdx;
	quint8 trackBuf[TRACKBUF_LEN_CRC];
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

	void updateIndicators(int drive);
	void updateSerialPort(void);
	quint16 calcChecksum(const quint8 *data, int length);
};

#endif


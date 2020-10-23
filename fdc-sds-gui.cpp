/**********************************************************************************
*
*  Altair FDC+ Serial Disk Server
*      This program serves Altair disk images over a high speed serial port
*      for computers running the FDC+ Enhanced Floppy Disk Controller.
*
*     Version     Date        Author         Notes
*      1.0     10/11/2020     P. Linstruth   Original
*
***********************************************************************************
*
* This version is based on the FDC+ Serial Drive Server 1.3 by Mike Douglas
*
***********************************************************************************
*
*  Communication with the server is over a serial port at 403.2K Baud, 8N1.
*  All transactions are initiated by the FDC. The second choice for baud rate
*  is 460.8K. Finally, 230.4K is the most likely supported baud rate on the PC
*  if 403.2K and 460.8K aren't avaialable.
*
*  403.2K is the preferred rate as it allows full-speed operation and is the
*  most accurate of the three baud rate choices on the FDC. 460.8K also allows
*  full speed operation, but the baud rate is off by about 3.5%. This works, but
*  is borderline. 230.4K is available on most all serial ports, is within 2% of
*  the FDC baud rate, but runs at 80%-90% of real disk speed.
*
*  FDC TO SERVER COMMANDS
*    Commands from the FDC to the server are fixed length, ten byte messages. The 
*    first four bytes are a command in ASCII, the remaining six bytes are grouped
*    as three 16 bit words (little endian). The checksum is the 16 bit sum of the
*    first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command       Parameter 1         Parameter 2           Checksum
*
*    Commands:
*      STAT - Provide and request drive status. The FDC sends the selected drive
*             number and head load status in Parameter 1 and the current track 
*             number in Parameter 2. The Server responds with drive mount status
*             (see below). The LSB of Parameter 1 contains the currently selected
*             drive number or 0xff is no drive is selected. The MSB of parameter 1
;             is non-zero if the head is loaded, zero if not loaded.
*
*             The FDC issues the STAT command about ten times per second so that
*             head status and track number information is updated quickly. The 
*             server may also want to assume the drive is selected, the head is
*             loaded, and update the track number whenever a READ is received.
*
*      READ - Read specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             length is in Parameter 2 and must be the track length. Also see
*             "Transfer of Track Data" below.
*
*      WRIT - Write specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             must be track length. Server responds with WRIT response when ready
*             for the FDC to send the track of data. See "Transfer of Track Data" below.
*
*
*  SERVER TO FDC 
*    Reponses from the server to the FDC are fixed length, ten byte messages. The 
*    first four bytes are a response command in ASCII, the remaining six bytes are
*    grouped as three 16 bit words (little endian). The checksum is the 16 bit sum
*    of the first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command      Response Code        Reponse Data          Checksum
*
*    Commands:
*      STAT - Returns drive status in Response Data with one bit per drive. "1" means a
*             drive image is mounted, "0" means not mounted. Bits 15-0 correspond to
*             drive numbers 15-0. Response code is ignored by the FDC.
*
*      WRIT - Issued in repsonse to a WRIT command from the FDC. This response is
*             used to tell the FDC that the server is ready to accept continuous transfer
*             of a full track of data (response code word set to "OK." If the request
*             can't be fulfilled (e.g., specified drive not mounted), the reponse code
*             is set to NOT READY. The Response Data word is don't care.
*
*      WSTA - Final status of the write command after receiving the track data is returned
*             in the repsonse code field. The Response Data word is don't care.
*
*    Reponse Code:
*      0x0000 - OK
*      0x0001 - Not Ready (e.g., write request to unmounted drive)
*      0x0002 - Checksum error (e.g., on the block of write data)
*      0x0003 - Write error (e.g., write to disk failed)
*
*
*  TRANSFER OF TRACK DATA
*    Track data is sent as a sequence of bytes followed by a 16 bit, little endian 
*    checksum. Note the Transfer Length field does NOT include the two bytes of
*    the checksum. The following notes apply to both the FDC and the server.
*
*  ERROR RECOVERY
*    The FDC uses a timeout of one second after the last byte of a message or data block
*        is sent to determine if a transmission was ignored.
*
*    The server should ignore commands with an invalid checksum. The FDC may retry the
*        command if no response is received. An invalid checksum on a block of write
*        data should not be ignored, instead, the WRIT response should have the
*        Reponse Code field set to 0x002, checksum error.
*
*    The FDC ignores responses with an invalid checksum. The FDC may retry the command
*        that generated the response by sending the command again.
*
***********************************************************************************/

#include <QtWidgets>
#include <QMessageBox>

#include "fdc-sds-gui.h"
#include "grnled.xpm"
#include "redled.xpm"

FDCDialog::FDCDialog(QWidget *parent)
	: QDialog(parent)
{
	int driveNum,row;

	// Title
	setWindowTitle(tr("FDC+ Serial Drive Server"));

	// Pixmaps
	grnLED = new QPixmap(greenled_xpm);
	redLED = new QPixmap(redled_xpm);

	// Layouts
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *commLayout = new QHBoxLayout;
	QGroupBox *driveGroup[MAX_DRIVE];
	QVBoxLayout *driveLayout[MAX_DRIVE];
	QHBoxLayout *row1Layout[MAX_DRIVE];
	QHBoxLayout *row2Layout[MAX_DRIVE];
	QHBoxLayout *row3Layout[MAX_DRIVE];
	QHBoxLayout *enabledLayout[MAX_DRIVE];
	QHBoxLayout *headloadLayout[MAX_DRIVE];
	QHBoxLayout *infoLayout = new QHBoxLayout;
	QVBoxLayout *dashboardLayout = new QVBoxLayout;

	// Setup drive widgets and status
	for (driveNum = 0; driveNum < MAX_DRIVE; driveNum++) {
		row1Layout[driveNum] = new QHBoxLayout;
		row2Layout[driveNum] = new QHBoxLayout;
		row3Layout[driveNum] = new QHBoxLayout;

		label = new QLabel(QString("Drive %1").arg(driveNum));
		row1Layout[driveNum]->addWidget(label);
		label = new QLabel(tr("Track"));
		row1Layout[driveNum]->addWidget(label);

		trackProgress[driveNum] = new QProgressBar;
		trackProgress[driveNum]->setMinimum(0);
		trackProgress[driveNum]->setFormat(QString("%v"));
		trackProgress[driveNum]->setAlignment(Qt::AlignLeft);  
		trackProgress[driveNum]->setTextVisible(false);
		row1Layout[driveNum]->addWidget(trackProgress[driveNum]);

		fileName[driveNum] = new QLineEdit;
		fileName[driveNum]->setReadOnly(true);
		fileName[driveNum]->setEnabled(false);
		row2Layout[driveNum]->addWidget(fileName[driveNum]);

		loadButton[driveNum] = new QPushButton(tr("Load"), this);
		unloadButton[driveNum] = new QPushButton(tr("Unload"), this);
		unloadButton[driveNum]->setEnabled(false);

		enabledLayout[driveNum] = new QHBoxLayout;
		enabledLayout[driveNum]->setAlignment(Qt::AlignRight);  
		label = new QLabel(tr("Enabled"));
		enabledLabel[driveNum] = new QLabel;
		enabledLabel[driveNum]->setPixmap(*redLED);
		enabledLayout[driveNum]->addWidget(label);
		enabledLayout[driveNum]->addWidget(enabledLabel[driveNum]);

		headloadLayout[driveNum] = new QHBoxLayout;
		headloadLayout[driveNum]->setAlignment(Qt::AlignRight);  
		label = new QLabel(tr("Head Load"));
		headloadLabel[driveNum] = new QLabel;
		headloadLabel[driveNum]->setPixmap(*redLED);
		headloadLayout[driveNum]->addWidget(label);
		headloadLayout[driveNum]->addWidget(headloadLabel[driveNum]);

		row3Layout[driveNum]->addWidget(loadButton[driveNum]);
		row3Layout[driveNum]->addWidget(unloadButton[driveNum]);
		row3Layout[driveNum]->addLayout(enabledLayout[driveNum]);
		row3Layout[driveNum]->addLayout(headloadLayout[driveNum]);

		connect(loadButton[driveNum], &QPushButton::clicked, [this, driveNum] { loadButtonSlot(driveNum); });
		connect(unloadButton[driveNum], &QPushButton::clicked, [this, driveNum] { unloadButtonSlot(driveNum); });

		driveLayout[driveNum] = new QVBoxLayout;
		driveLayout[driveNum]->addLayout(row1Layout[driveNum]);
		driveLayout[driveNum]->addLayout(row2Layout[driveNum]);
		driveLayout[driveNum]->addLayout(row3Layout[driveNum]);

		driveGroup[driveNum] = new QGroupBox;
		driveGroup[driveNum]->setLayout(driveLayout[driveNum]);

		curTrack[driveNum] = 0;
		enableStatus[driveNum] = false;
		headStatus[driveNum] = false;

		openMode[driveNum] = QIODevice::ReadWrite;
		driveFile[driveNum] = new QFile;
	}

	// Information
	label = new QLabel(tr("FDC+ Serial Drive Server v1.0 BETA"));
	infoLayout->addWidget(label);
	label = new QLabel(tr("(c)2020 Deltec Enterprises"));
	label->setAlignment(Qt::AlignRight);  
	infoLayout->addWidget(label);

	// Communications Ports
	serialPortBox = new QComboBox;
	serialPorts = QSerialPortInfo::availablePorts();
	for (const QSerialPortInfo &info : serialPorts) {
		serialPortBox->addItem(info.portName());
	}
	serialPortBox->setCurrentIndex(-1);
	connect(serialPortBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){ serialPortSlot(index); });

	commLayout->addWidget(serialPortBox);

	baudRateBox = new QComboBox;
	baudRateBox->addItem("230.4K", 230400);
	baudRateBox->addItem("403.2K", 403200);
	baudRateBox->addItem("460.8K", 460800);
	connect(baudRateBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){ baudRateSlot(index); });

	commLayout->addWidget(baudRateBox);

	mainLayout->addLayout(commLayout);

	for (driveNum = 0; driveNum < MAX_DRIVE; driveNum++) {
		mainLayout->addWidget(driveGroup[driveNum]);
	}

	// Dashboard
	QFont monoFont("Courier New", 10);

	for (row = 0; row < DASHBOARD_ROWS; row++) {
		dashboardLabel[row] = new QLabel;
		dashboardLabel[row]->setFont(monoFont);
		dashboardLayout->addWidget(dashboardLabel[row]);
	}

	dashboardLabel[DASHBOARD_STAT]->setText(QString("STAT").leftJustified(80));
	dashboardLabel[DASHBOARD_READ]->setText(QString("READ").leftJustified(80));
	dashboardLabel[DASHBOARD_WRIT]->setText(QString("WRIT").leftJustified(80));
	dashboardLabel[DASHBOARD_ERR]->setText(QString("ERROR").leftJustified(80));

	mainLayout->addLayout(dashboardLayout);

	// Information Line
	mainLayout->addLayout(infoLayout);

	setLayout(mainLayout);

	// Serial Port Object
	serialPort = new QSerialPort;
	baudRate = baudRateBox->currentData().toInt();

	// Start timer
	timer = new QTimer(this);
	timer->setTimerType(Qt::PreciseTimer);
	connect(timer, &QTimer::timeout, this, &FDCDialog::timerSlot);
	timer->start(100);	// 100ms timer

	// Counters
	tickCount = 0;
	statCount = 0;
	readCount = 0;
	writCount = 0;
	errCount = 0;
	rbyteCount = 0;
	wbyteCount = 0;

	savePath = QCoreApplication::applicationDirPath();

	// MacOS stores the application in <dir>/<name>.app/Contents/MacOS
	// If running on MacOS, move up 3 directories to get in the actual
	// application launch directory. It's a shame that Qt5 doesn't
	// provide a "standard" method of doing this.
#ifdef Q_OS_MAC
	savePath = savePath + "../../../";
#endif

#ifdef DEBUG
	// create debug window
	dbgWindow = new DbgWidget();
	dbgWindow->setGeometry(0, 0, 600, 400);
	dbgWindow->setWindowTitle(tr("FDC+ Serial Drive Server Debug Output"));
	dbgWindow->show();
#endif
}

void FDCDialog::serialPortSlot(int index)
{
	serialPort->setPortName(serialPortBox->itemText(index));

	updateSerialPort();
}

void FDCDialog::baudRateSlot(int index)
{
	baudRate = baudRateBox->itemData(index).toInt();

	updateSerialPort();
}

void FDCDialog::loadButtonSlot(int drive)
{
	QString fname = QFileDialog::getOpenFileName(this, tr("Open Disk Image"), savePath, tr("Disk Image Files (*.dsk);;All Files (*.*)"));

	if (fname.length()) {
		driveFile[drive]->setFileName(fname);

		if (driveFile[drive]->open(openMode[drive])) {
			QFileInfo finfo(fname);

			savePath = finfo.filePath();

			fileName[drive]->setText(finfo.fileName());
			fileName[drive]->setEnabled(true);

      			qint64 filesize = driveFile[drive]->size();
			if (filesize < 200000) {
				maxTrack[drive] = 34;
			}
			else if (filesize < 500000) {
				maxTrack[drive] = 76;
			}
			else {
				maxTrack[drive] = 2047;
			}
			trackProgress[drive]->setMaximum(maxTrack[drive]);
			trackProgress[drive]->setValue(maxTrack[drive]);
			trackProgress[drive]->setTextVisible(true);

			loadButton[drive]->setEnabled(false);
			unloadButton[drive]->setEnabled(true);
			unloadButton[drive]->setFocus(Qt::OtherFocusReason);
		}
		else {
			QMessageBox::critical(this, "Mount Error", QString("Could not open disk mage '%1'").arg(fname));
		}
	}
}

void FDCDialog::unloadButtonSlot(int drive)
{
	if (driveFile[drive]->isOpen()) {
		driveFile[drive]->close();

		fileName[drive]->setText(QString(""));
		fileName[drive]->setEnabled(false);

		trackProgress[drive]->setTextVisible(false);

		maxTrack[drive] = 0;
		curTrack[drive] = 0;
		enableStatus[drive] = false;
		headStatus[drive] = false;

		updateIndicators();

		loadButton[drive]->setEnabled(true);
		loadButton[drive]->setFocus(Qt::OtherFocusReason);
		unloadButton[drive]->setEnabled(false);
	}
}

void FDCDialog::timerSlot()
{
	// Reset timer
	tickCount++;

	// Clear last error text
	if (errTimeout) {
		if (--errTimeout == 0) {
			clearError();
		}
	}
}

void FDCDialog::readyReadSlot()
{
	int i;
	quint16 driveNum;
	quint16 trackLen;
	qint64 bytesRead;
	quint16 checksum;

#if 0
	if (!serialPort->isOpen()) {
		return;
	}

	if (!serialPort->bytesAvailable()) {
		return;
	}
#endif

	bytesRead = readSerialPort(cmdBuf.asBytes, CMDBUF_SIZE, 50);

	if (bytesRead < CMDBUF_SIZE) {
		displayError(QString("received partial command buffer %1/10 bytes").arg(bytesRead));
		dbgWindow->hexDump(cmdBuf.asBytes, bytesRead);
		return;
	}

	// Calculate and validate checksum
	checksum = calcChecksum(cmdBuf.asBytes, CMD_LEN);

	if (checksum != cmdBuf.checksum) {
		displayError(QString("CRC ERROR calc=%1 recv=%2").arg(checksum,4,16).arg(cmdBuf.checksum,4,16));
	}

	// READ command
	if (QString(cmdBuf.command).left(4) == QString("READ")) {
		readCount++;

		driveNum = cmdBuf.param1 >> 12;

		displayDash(QString("%1").arg(readCount,6,10,QChar('0')), DASHBOARD_READ, 6, 6);
		displayDash(QString("0x%1").arg(driveNum,2,16,QChar('0')), DASHBOARD_READ, 14, 4);
		displayDash(QString("0x%1").arg(cmdBuf.param1 & 0x0fff,4,16,QChar('0')), DASHBOARD_READ, 20, 6);
		displayDash(QString("0x%1").arg(cmdBuf.param2,4,16,QChar('0')), DASHBOARD_READ, 28, 6);

		// Ignore invalid drive numbers
		if (driveNum > MAX_DRIVE) {
			return;
		}

		enableDrive(driveNum);
		enableHead(driveNum);

		// If drive not mounted, ignore
		if (!driveFile[driveNum]->isOpen()) {
			displayError(QString("READ error - drive %1 not loaded").arg(driveNum));
			return;
		}

		// Track in lower 12 bits
		curTrack[driveNum] = cmdBuf.param1 & 0x0fff;
		trackLen = cmdBuf.param2;

		// If the requested track length is too long, ignore
		if (trackLen > TRKBUF_SIZE) {
			displayError(QString("READ requested track len %1 > %2 bytes").arg(trackLen).arg(TRKBUF_SIZE));
			return;
		}

		if (curTrack[driveNum] > maxTrack[driveNum]) {
			displayError(QString("READ requested track %1 > %2").arg(curTrack[driveNum]).arg(maxTrack[driveNum]));
			return;
		}

		updateIndicators();

		driveFile[driveNum]->seek(curTrack[driveNum] * trackLen);
		if (driveFile[driveNum]->pos() != curTrack[driveNum] * trackLen) {
			displayError(QString("read() error seeking to %1").arg(curTrack[driveNum] * trackLen));
		}

		if ((bytesRead = driveFile[driveNum]->read((char *) trkBuf, trackLen)) != trackLen) {
			displayError(QString("read() failed - read %1 of %2 bytes").arg(bytesRead).arg(trackLen));
			return;	// Ignore reads past end of file
		}

		checksum = calcChecksum(trkBuf, trackLen);
		trkBuf[trackLen] = checksum & 0x00ff;			// LSB of checksum
		trkBuf[trackLen+1] = (checksum >> 8) & 0x00ff;	// MSB of checksum

		writeSerialPort(trkBuf, trackLen + CRC_LEN);
	}

	// WRIT command
	else if (QString(cmdBuf.command).left(4) == QString("WRIT")) {
		writCount++;

		driveNum = cmdBuf.param1 >> 12;

		displayDash(QString("%1").arg(writCount,6,10,QChar('0')), DASHBOARD_WRIT, 6, 6);
		displayDash(QString("0x%1").arg(driveNum,2,16,QChar('0')), DASHBOARD_WRIT, 14, 4);
		displayDash(QString("0x%1").arg(cmdBuf.param1 & 0x0fff,4,16,QChar('0')), DASHBOARD_WRIT, 20, 6);
		displayDash(QString("0x%1").arg(cmdBuf.param2,4,16,QChar('0')), DASHBOARD_WRIT, 28, 6);

		// Ignore invalid drive numbers
		if (driveNum > MAX_DRIVE) {
			return;
		}

		curTrack[driveNum] = cmdBuf.param1 & 0x0fff;
		trackLen = cmdBuf.param2;

		enableDrive(driveNum);
		enableHead(driveNum);

		// If drive not mounted, ignore
		if (!driveFile[driveNum]->isOpen()) {
			displayError(QString("WRIT error - drive %1 not loaded").arg(driveNum));
			cmdBuf.rcode = STAT_NOT_READY;
		}
		else {
			cmdBuf.rcode = STAT_OK;
		}

		// If the requested track length is too long, ignore
		if (trackLen > TRKBUF_SIZE) {
			displayError(QString("WRIT requested track len %1 > %2 bytes").arg(trackLen).arg(TRKBUF_SIZE));
			cmdBuf.rcode = STAT_NOT_READY;
		}

		if (curTrack[driveNum] > maxTrack[driveNum]) {
			displayError(QString("WRIT requested track %1 > %2").arg(curTrack[driveNum]).arg(maxTrack[driveNum]));
			return;
		}

		// Send WRIT response
		cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, CMD_LEN);

		if (cmdBuf.rcode == STAT_OK) {
			writeSerialPort(cmdBuf.asBytes, CMDBUF_SIZE);

			int bytesRead = readSerialPort(trkBuf, trackLen + CRC_LEN, 250);	// Length of track plus 2 byte CRC

			checksum = calcChecksum(trkBuf, trackLen);

			if (bytesRead != trackLen + CRC_LEN) {
				displayError(QString("WRIT received wrong track length (%1/%2)").arg(bytesRead).arg(trackLen + CRC_LEN));
				cmdBuf.rcode = STAT_CHECKSUM_ERR;
			}
			else if (((checksum & 0xff) == trkBuf[trackLen])
				&& (((checksum >> 8) & 0xff)) == trkBuf[trackLen + 1]) {

				if (!(driveFile[driveNum]->seek(curTrack[driveNum] * trackLen))) {
					displayError(QString("WRIT error seeking to %1").arg(curTrack[driveNum] * trackLen));
					cmdBuf.rcode = STAT_WRITE_ERR;
				}
				else if (driveFile[driveNum]->write((char *) trkBuf, trackLen) != trackLen) {
					displayError("WRIT file write error");
					cmdBuf.rcode = STAT_WRITE_ERR;
				}
			}
			else {
				displayError(QString("WRIT track checksum error"));
				cmdBuf.rcode = STAT_CHECKSUM_ERR;
			}

			// Set response to WSTA
			cmdBuf.command[0] = 'W';
			cmdBuf.command[1] = 'S';
			cmdBuf.command[2] = 'T';
			cmdBuf.command[3] = 'A';
			cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, CMD_LEN);
		}

		writeSerialPort(cmdBuf.asBytes, CMDBUF_SIZE);
	}

	// STAT command
	else if (QString(cmdBuf.command).left(4) == QString("STAT")) {
		statCount++;

		enableDrive(0xff);
		enableHead(0xff);

		driveNum = cmdBuf.param1 & 0x00ff;

		if (driveNum < MAX_DRIVE) {
			enableStatus[driveNum] = true;
			headStatus[driveNum] = (cmdBuf.param1 & 0xff00) >> 8;
			curTrack[driveNum] = cmdBuf.param2;
		}

		updateIndicators();

		displayDash(QString("%1").arg(statCount,6,10,QChar('0')), DASHBOARD_STAT, 6, 6);
		displayDash(QString("0x%1").arg(driveNum,2,16,QChar('0')), DASHBOARD_STAT, 14, 4);
		displayDash(QString("0x%1").arg(cmdBuf.param1,4,16,QChar('0')), DASHBOARD_STAT, 20, 6);
		displayDash(QString("0x%1").arg(cmdBuf.param2,4,16,QChar('0')), DASHBOARD_STAT, 28, 6);

		// Respond with status of mounted drives
		cmdBuf.rcode = STAT_OK;
		cmdBuf.rdata = 0;
		for (i = 0; i < MAX_DRIVE; i++) {
			if (driveFile[i]->isOpen()) {
				cmdBuf.rdata |= (1 << i);
			}
		}

		cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, CMD_LEN);

		writeSerialPort(cmdBuf.asBytes, CMDBUF_SIZE);
	}
	else {
		displayError(QString("Received unknown command"));
	}
}

void FDCDialog::updateSerialPort()
{
	if (serialPort->isOpen()) {
		disconnect(serialPort);
		serialPort->clear();
		serialPort->close();
	}

	if (serialPortBox->currentIndex() == -1) {
		return;
	}

	if (serialPort->open(QIODevice::ReadWrite)) {
		if (serialPort->setBaudRate(baudRate) == false) {
			QMessageBox::critical(this,
				"Serial Port Error",
				QString("Could not set baudrate to %1").arg(baudRate));
		}
		serialPort->setDataBits(QSerialPort::Data8);
		serialPort->setParity(QSerialPort::NoParity);
		serialPort->setStopBits(QSerialPort::OneStop);
		serialPort->setFlowControl(QSerialPort::NoFlowControl);
		serialPort->setDataTerminalReady(true);
		serialPort->setRequestToSend(true);
		serialPort->clear();

		connect(serialPort, &QSerialPort::readyRead, this, &FDCDialog::readyReadSlot);
	}
	else {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString("Could not open serial port '%1' (%2)").arg(serialPort->portName()).arg(serialPort->error()));

		serialPortBox->setCurrentIndex(-1);
	}
}

//
// Read len bytes into buffer with msec timeout
//
int FDCDialog::readSerialPort(const quint8 *buffer, int len, qint64 msec)
{
	int i = 0;

	if (!serialPort->isOpen()) {
		return -1;
	}

	QElapsedTimer timeout;
	timeout.start();

	i += serialPort->read((char *) buffer, len);

	while (i != -1 && i < len && !timeout.hasExpired(msec)) {
		serialPort->waitForReadyRead(10);	// Get more characters
		i += serialPort->read((char *) buffer+i, len-i);
	}

	rbyteCount += i;

	return i;
}

//
// The FDC+ serial protocol is synchronous. Wait until the entire
// buffer is sent. Returns -1 on error, true if all bytes were sent,
// false if timeout.
//
int FDCDialog::writeSerialPort(const quint8 *buffer, int len, qint64 msec)
{
	if (!len) {
		displayError(QString("write serial port len=%1").arg(len));
		return -1;
	}

	serialPort->write((char *) buffer, len);

	wbyteCount += len;

	// All writes are synchronous
	serialPort->waitForBytesWritten(msec);

	return !serialPort->bytesToWrite();
}
void FDCDialog::updateIndicators()
{
	int drive;

	for (drive = 0; drive < MAX_DRIVE; drive++) {
		enabledLabel[drive]->setPixmap(*redLED);
		headloadLabel[drive]->setPixmap(*redLED);

		if (enableStatus[drive]) {
			enabledLabel[drive]->setPixmap(*grnLED);
		}
		if (headStatus[drive]) {
			headloadLabel[drive]->setPixmap(*grnLED);
		}
		if (maxTrack[drive] >= curTrack[drive]) {
			trackProgress[drive]->setValue(curTrack[drive]);
		}
	}
}

quint16 FDCDialog::calcChecksum(const quint8 *data, int length)
{
	int i;
	quint16 checksum;

	checksum = 0;

	for (i = 0; i < length; i++) {
		checksum += data[i];
	}

	return checksum;
}

void FDCDialog::enableDrive(quint8 driveNum)
{
	quint8 drive;

	for (drive = 0; drive < MAX_DRIVE; drive++) {
		enableStatus[drive] = false;
	}

	if (driveNum < MAX_DRIVE) {
		enableStatus[driveNum] = true;
	}
}

void FDCDialog::enableHead(quint8 driveNum)
{
	quint8 drive;

	for (drive = 0; drive < MAX_DRIVE; drive++) {
		headStatus[drive] = false;
	}

	if (driveNum < MAX_DRIVE) {
		headStatus[driveNum] = true;
	}
}

void FDCDialog::displayDash(QString text, int row, int pos, int len)
{
	dashboardLabel[row]->setText(dashboardLabel[row]->text().replace(pos, len, text));
}

void FDCDialog::displayError(QString text)
{
	errCount++;
	displayDash(QString("%1").arg(errCount,6,10,QChar('0')), DASHBOARD_ERR, 6, 6);
	displayDash(text.leftJustified(74), DASHBOARD_ERR, 14, 74);
	errTimeout = DASHBOARD_ERRTO;
}

void FDCDialog::clearError()
{
	displayDash(QString("").leftJustified(74), DASHBOARD_ERR, 14, 74);
	errTimeout = DASHBOARD_ERRTO;
}

void FDCDialog::reject()
{
	QMessageBox::StandardButton r;

	r = QMessageBox::question(this, "Exit Program", tr("Are you sure?"), QMessageBox::No | QMessageBox::Yes);

	if (r != QMessageBox::Yes) {
		return;
	}

	if (serialPort->isOpen()) {
		serialPort->close();
	}

	for (int driveNum = 0; driveNum < MAX_DRIVE; driveNum++) {
		if (driveFile[driveNum]->isOpen()) {
			driveFile[driveNum]->close();
		}
	}

#ifdef DEBUG
	// Close debug window
	dbgWindow->close();
#endif

	QDialog::reject();
}

DbgWidget::DbgWidget(QWidget *parent)
        : QTextEdit(parent)
{
}

void DbgWidget::hexDump(const quint8 *buffer, int len)
{
	QString line;

	while (len--) {
		line += QString().asprintf("'%c' %02x ", (QChar::isPrint(*buffer)) ? *buffer : '.', *buffer);
		buffer++;
	}

	append(line);
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setStyle(QStyleFactory::create("Fusion"));
	FDCDialog *dialog = new FDCDialog;
	dialog->show();
	return app.exec();
}



/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtSerialBus module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "libmodbusslave.h"

#include <QtCore/qdebug.h>

#if defined(Q_OS_WIN)
#include <winsock2.h>
#elif defined(Q_OS_UNIX)
#include <sys/select.h>
#endif

#define MODBUS_SERIAL_ADU_SIZE 256
#define FUNCTION_ID 1
#define START_ADDRESS_HI 2
#define START_ADDRESS_LO 3
#define QUANTITY_HI 4
#define QUANTITY_LO 5

QT_BEGIN_NAMESPACE

LibModBusSlave::LibModBusSlave() :
    QModBusSlave(),
    serialPort(0),
    context(0),
    mapping(modbus_mapping_new(0,0,0,0)),
    connected(false),
    slave(1)
{
    qRegisterMetaType<QModBusDevice::ModBusTable>("QModBusDevice::ModBusTable");
}

LibModBusSlave::~LibModBusSlave()
{
    close();
    modbus_mapping_free(mapping);
    mapping = 0;
}

bool LibModBusSlave::setDevice(QIODevice *transport, ApplicationDataUnit ADU)
{
    //Only serialport supported at the moment
    adu = ADU;
    serialPort = qobject_cast<QSerialPort*>(transport);
    if (!serialPort)
        return false;

    return true;
}

bool LibModBusSlave::setMap(QModBusDevice::ModBusTable table, quint16 size)
{
    if (connected) {
        setError(tr("Cannot set maps when slave is connected to the network."),
                 QModBusDevice::WriteError);
        return false;
    }

    mappingTable[table] = size;

    return true;
}

void LibModBusSlave::handleError(int errorNumber)
{
    setError(qt_error_string(errorNumber), QModBusDevice::ReadError);
    close();
}

bool LibModBusSlave::open()
{
    if (connected)
        return true;

    if (!serialPort) {
        setError(tr("No transport device specified."), QModBusDevice::ConnectionError);
        return false;
    }

    QChar parity;

    switch (serialPort->parity()) {
    case QSerialPort::NoParity:
        parity = 'N';
        break;
    case QSerialPort::EvenParity:
        parity = 'E';
        break;
    case QSerialPort::OddParity:
        parity = 'O';
        break;
    default:
        setError(tr("Unsupported parity."), QModBusDevice::ConnectionError);
        return false;
    }

    QString location = portNameToSystemLocation(serialPort->portName());

    context = modbus_new_rtu(location.toLatin1(),
                             serialPort->baudRate(),
                             parity.toLatin1(),
                             serialPort->dataBits(),
                             serialPort->stopBits());
    if (context == NULL) {
        setError(qt_error_string(errno), QModBusDevice::ConnectionError);
        return false;
    }
    modbus_set_slave(context, slave);

    modbus_set_debug(context, TRUE);
    if (modbus_connect(context) == -1) {
        setError(qt_error_string(errno), QModBusDevice::ConnectionError);
        close();
        return false;
    }

    if (mapping) {
        modbus_mapping_free(mapping);
        mapping = 0;
    }
    mapping = modbus_mapping_new(mappingTable[QModBusDevice::Coils],
                                 mappingTable[QModBusDevice::DiscreteInputs],
                                 mappingTable[QModBusDevice::HoldingRegisters],
                                 mappingTable[QModBusDevice::InputRegisters]);
    if (mapping == NULL) {
        setError(qt_error_string(errno), QModBusDevice::ConnectionError);
        return false;
    }

    listener = new ListenThread();
    listener->context = context;
    listener->mapping = mapping;
    listener->moveToThread(&thread);
    connect(&thread, &QThread::finished, listener.data(), &QObject::deleteLater);
    connect(this, &LibModBusSlave::operate, listener.data(), &ListenThread::doWork);
    connect(listener.data(), &ListenThread::error, this, &LibModBusSlave::handleError);
    connect(listener.data(), &ListenThread::slaveRead, this, &LibModBusSlave::slaveRead);
    connect(listener.data(), &ListenThread::slaveWritten, this, &LibModBusSlave::slaveWritten);
    thread.start();
    emit operate();
    connected = true;
    setState(QModBusDevice::ConnectedState);
    return true;
}

void LibModBusSlave::close()
{
    thread.requestInterruption();
    thread.quit();
    thread.wait();
    listener.clear();

    modbus_close(context);
    modbus_free(context);
    context = 0;
    connected = false;
    setState(QModBusDevice::UnconnectedState);
}

int LibModBusSlave::slaveId() const
{
    return slave;
}

void LibModBusSlave::setSlaveId(int id)
{
    slave = id;
    modbus_set_slave(context, slave);
}

bool LibModBusSlave::data(QModBusDevice::ModBusTable table, quint16 address, quint16& data)
{
    if (mappingTable[table] >= address) {
        switch (table) {
            case QModBusDevice::DiscreteInputs:
                data = mapping->tab_input_bits[address];
                break;
            case QModBusDevice::Coils:
                data = mapping->tab_bits[address];
                break;
            case QModBusDevice::InputRegisters:
                data = mapping->tab_input_registers[address];
                break;
            case QModBusDevice::HoldingRegisters:
                data = mapping->tab_registers[address];
                break;
        }
    } else {
        setError(tr("ReadError: invalid parameters."), QModBusDevice::ReadError);
        return false;
    }

    return true;
}

bool LibModBusSlave::setData(QModBusDevice::ModBusTable table, quint16 address, quint16 data)
{
    if (mappingTable[table] >= address) {
        switch (table) {
            case QModBusDevice::DiscreteInputs:
                mapping->tab_input_bits[address] = (uint8_t)data;
                break;
            case QModBusDevice::Coils:
                mapping->tab_bits[address] = (uint8_t)data;
                break;
            case QModBusDevice::InputRegisters:
                mapping->tab_input_registers[address] = data;
                break;
            case QModBusDevice::HoldingRegisters:
                mapping->tab_registers[address] = data;
                break;
        }
    } else {
        setError(tr("WriteError: invalid parameters."), QModBusDevice::WriteError);
        return false;
    }

    return true;
}

QString LibModBusSlave::portNameToSystemLocation(QString source)
{
#if defined(Q_OS_WINCE)
    return source.endsWith(QLatin1Char(':'))
            ? source : (source + QLatin1Char(':'));
#elif defined(Q_OS_WIN32)
    return source.startsWith(QLatin1String("COM"))
            ? (QLatin1String("\\\\.\\") + source) : source;
#elif defined(Q_OS_UNIX)
    return (source.startsWith(QLatin1Char('/'))
            || source.startsWith(QLatin1String("./"))
            || source.startsWith(QLatin1String("../")))
            ? source : (QLatin1String("/dev/") + source);
#else
#  error Unsupported OS
#endif
}

void ListenThread::doWork()
{
    quint8 query[MODBUS_SERIAL_ADU_SIZE];

    while (!QThread::currentThread()->isInterruptionRequested()) {
        // The FD_ZERO and FD_SET need to be done at every loop.
        int socket = modbus_get_socket(context);
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(socket, &rset);

        // Because modbus_receive doesn't obey timeouts properly, first wait with select until there
        // is something to read.
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int req = select(socket + 1, &rset, NULL, NULL, &timeout);

        // Read only if select returned that there is something to read
        if (req > 0)
            req = modbus_receive(context, query);

        // Check for errors on both select and modbus_receive
        if (req == -1) {
            emit error(errno);
            break;
        }

        // Send reply only if data was received.
        if (req > 1) {
            modbus_reply(context, query, req, mapping);

            //emit proper signal(s)
            const int functionId = query[FUNCTION_ID];
            quint16 startAddress = 0;
            quint16 quantity = 0;
            QModBusDevice::ModBusTable table;
            switch (functionId) {
            case ReadCoils:
            case ReadDiscreteInputs:
            case ReadHoldingRegisters:
            case ReadInputRegisters:
                emit slaveRead();
                break;
            case WriteSingleCoil:
                table = QModBusDevice::Coils;
                startAddress = (query[START_ADDRESS_HI] << 8) + query[START_ADDRESS_LO];
                emit slaveWritten(table, startAddress, 1);
                break;
            case WriteMultipleCoils:
                table = QModBusDevice::Coils;
                startAddress = (query[START_ADDRESS_HI] << 8) + query[START_ADDRESS_LO];
                quantity = (query[QUANTITY_HI] << 8) + query[QUANTITY_LO];
                emit slaveWritten(table, startAddress, quantity);
                break;
            case WriteSingleRegister:
                table = QModBusDevice::HoldingRegisters;
                startAddress = (query[START_ADDRESS_HI] << 8) + query[START_ADDRESS_LO];
                emit slaveWritten(table, startAddress, 1);
                break;
            case WriteMultipleRegisters:
                table = QModBusDevice::HoldingRegisters;
                startAddress = (query[START_ADDRESS_HI] << 8) + query[START_ADDRESS_LO];
                quantity = (query[QUANTITY_HI] << 8) + query[QUANTITY_LO];
                emit slaveWritten(table, startAddress, quantity);
                break;
            case ReadWriteRegisters:
                emit slaveRead();
                table = QModBusDevice::HoldingRegisters;
                startAddress = (query[START_ADDRESS_HI] << 8) + query[START_ADDRESS_LO];
                quantity = (query[QUANTITY_HI] << 8) + query[QUANTITY_LO];
                emit slaveWritten(table, startAddress, quantity);
                break;
            default:
                break;
            }
        }
    }
}

QT_END_NAMESPACE
/****************************************************************************
** Copyright (c) 2012 Doug Brown
** All right reserved.
** Web: http://code.google.com/p/qextserialport/
**
** Permission is hereby granted, free of charge, to any person obtaining
** a copy of this software and associated documentation files (the
** "Software"), to deal in the Software without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Software, and to
** permit persons to whom the Software is furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
** MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
** NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
** LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
** OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
** WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**
****************************************************************************/

#include "qextserialenumerator.h"
#include "qextserialenumerator_p.h"
#include <QtCore/QDebug>

void QextSerialEnumeratorPrivate::platformSpecificInit()
{
    monitor = NULL;
    notifierFd = -1;
    notifier = NULL;

    udev = udev_new();
    if (!udev)
        qCritical() << "Unable to initialize udev notifications";
}

void QextSerialEnumeratorPrivate::platformSpecificDestruct()
{
    if (notifier) {
        notifier->setEnabled(false);
        delete notifier;
    }

    if (monitor)
        udev_monitor_unref(monitor);

    if (udev)
        udev_unref(udev);
}

static QextPortInfo portInfoFromDevice(struct udev_device *dev)
{
    QString vendor = QString::fromLatin1(udev_device_get_property_value(dev, "ID_VENDOR_ID"));
    QString product = QString::fromLatin1(udev_device_get_property_value(dev, "ID_MODEL_ID"));

    QextPortInfo pi;
    pi.vendorID = vendor.toInt(0, 16);
    pi.productID = product.toInt(0, 16);
    pi.portName = QString::fromLatin1(udev_device_get_devnode(dev));
    pi.physName = pi.portName;

    return pi;
}

QList<QextPortInfo> QextSerialEnumeratorPrivate::getPorts_sys()
{
    QList<QextPortInfo> returnVal;
    struct udev *ud = udev_new();
    if (!ud) {
        qCritical() << "Unable to enumerate ports because udev is not initialized.";
        return returnVal;
    }

    struct udev_enumerate *enumerate = udev_enumerate_new(ud);
    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);
    struct udev_list_entry *list = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *entry = new udev_list_entry;
    
    udev_list_entry_foreach(entry, list) {
        const char *path = NULL;
        struct udev_device *dev = NULL;

        // Have to grab the actual udev device here...
        try{
        path = udev_list_entry_get_name(entry);
        }
        catch(...){
            delete entry;
            throw;
        }
        try{
        dev = udev_device_new_from_syspath(ud, path);
        }
        catch(...){
            delete ud;
            delete path;
            throw;
        }
        returnVal.append(portInfoFromDevice(dev));

        // Done with this device
        try{
        udev_device_unref(dev);
        }
        catch(...){
            delete dev;
            throw;
        }
    }
    // Done with the list and this udev
    try{
    udev_enumerate_unref(enumerate);
    }
    catch(...){
        delete enumerate;
        throw;
    }
    try{
    udev_unref(ud);
    }
    catch(...){
        delete ud;
        throw;
    }

    return returnVal;
}

bool QextSerialEnumeratorPrivate::setUpNotifications_sys(bool setup)
{
    Q_UNUSED(setup);
    Q_Q(QextSerialEnumerator);
    if (!udev) {
        qCritical() << "Unable to initialize notifications because udev is not initialized.";
        return false;
    }

    // Emit signals immediately for devices already connected (Windows version seems to behave
    // this way)
    foreach (QextPortInfo i, getPorts_sys())
        Q_EMIT q->deviceDiscovered(i);

    // Look for tty devices from udev.
    monitor = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(monitor, "tty", NULL);
    udev_monitor_enable_receiving(monitor);
    notifierFd = udev_monitor_get_fd(monitor);
    notifier = new QSocketNotifier(notifierFd, QSocketNotifier::Read);
    q->connect(notifier, SIGNAL(activated(int)), q, SLOT(_q_deviceEvent()));
    notifier->setEnabled(true);

    return true;
}

void QextSerialEnumeratorPrivate::_q_deviceEvent()
{
    Q_Q(QextSerialEnumerator);
    struct udev_device *dev = udev_monitor_receive_device(monitor);
    if (dev) {
        try{
        QextPortInfo pi = portInfoFromDevice(dev);
        QLatin1String action(udev_device_get_action(dev));
        udev_device_unref(dev);
        }
        catch(...){
            delete dev;
            throw;
        }

        if (action == QLatin1String("add"))
            Q_EMIT q->deviceDiscovered(pi);
        else if (action == QLatin1String("remove"))
            Q_EMIT q->deviceRemoved(pi);
    }
}

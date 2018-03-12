/**
 ******************************************************************************
 *
 * @file       treeitem.cpp
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup UAVObjectBrowserPlugin UAVObject Browser Plugin
 * @{
 * @brief The UAVObject Browser gadget plugin
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "treeitem.h"

/* Constructor */
HighLightManager::HighLightManager()
{
    // Initialize the timer and connect it to the callback
    m_expirationTimer.setTimerType(Qt::PreciseTimer);
    m_expirationTimer.setSingleShot(true);
    connect(&m_expirationTimer, &QTimer::timeout, this, &HighLightManager::checkItemsExpired);
}

/*
 * Called to add item to list. Item is only added if absent.
 * Returns true if item was added, otherwise false.
 */
bool HighLightManager::add(TreeItem *itemToAdd)
{
    // Lock to ensure thread safety
    QMutexLocker locker(&m_mutex);

    // Check so that the item isn't already in the list
    if (!m_items.contains(itemToAdd)) {
        m_items.insert(itemToAdd);
        return true;
    }
    return false;
}

bool HighLightManager::startTimer(QTime expirationTime)
{
    // Lock to ensure thread safety
    QMutexLocker locker(&m_mutex);

    if (!m_expirationTimer.isActive()) {
        int msec = QTime::currentTime().msecsTo(expirationTime);
        // qDebug() << "start" << msec;
        m_expirationTimer.start((msec < 10) ? 10 : msec);
        return true;
    }
    return false;
}

/*
 * Called to remove item from list.
 * Returns true if item was removed, otherwise false.
 */
bool HighLightManager::remove(TreeItem *itemToRemove)
{
    // Lock to ensure thread safety
    QMutexLocker locker(&m_mutex);

    // Remove item and return result
    return m_items.remove(itemToRemove);
}

/*
 * Callback called periodically by the timer.
 * This method checks for expired highlights and
 * removes them if they are expired.
 * Expired highlights are restored.
 */
void HighLightManager::checkItemsExpired()
{
    // Lock to ensure thread safety
    QMutexLocker locker(&m_mutex);

    // Get a mutable iterator for the list
    QMutableSetIterator<TreeItem *> iter(m_items);

    // This is the timestamp to compare with
    QTime now = QTime::currentTime();
    QTime next;

    // Loop over all items, check if they expired.
    while (iter.hasNext()) {
        TreeItem *item = iter.next();
        if (item->getHiglightExpires() <= now) {
            // expired, call removeHighlight
            item->removeHighlight();

            // Remove from list since it is restored.
            iter.remove();
        } else {
            // not expired, check if next to expire
            if (!next.isValid() || (next > item->getHiglightExpires())) {
                next = item->getHiglightExpires();
            }
        }
    }
    if (next.isValid()) {
        int msec = QTime::currentTime().msecsTo(next);
        // qDebug() << "restart" << msec;
        m_expirationTimer.start((msec < 10) ? 10 : msec);
    }
}

int TreeItem::m_highlightTimeMs = 300;

TreeItem::TreeItem(const QList<QVariant> &data, TreeItem *parent) :
    QObject(0),
    m_data(data),
    m_parent(parent),
    m_highlight(false),
    m_changed(false),
    m_highlightManager(0)
{}

TreeItem::TreeItem(const QVariant &data, TreeItem *parent) :
    QObject(0),
    m_parent(parent),
    m_highlight(false),
    m_changed(false),
    m_highlightManager(0)
{
    m_data << data << "" << "";
}

TreeItem::~TreeItem()
{
    qDeleteAll(m_children);
}

void TreeItem::appendChild(TreeItem *child)
{
    m_children.append(child);
    child->setParentTree(this);
}

void TreeItem::insertChild(TreeItem *child)
{
    int index = nameIndex(child->data(0).toString());

    m_children.insert(index, child);
    child->setParentTree(this);
}

TreeItem *TreeItem::getChild(int index) const
{
    return m_children.value(index);
}

int TreeItem::childCount() const
{
    return m_children.count();
}

int TreeItem::row() const
{
    if (m_parent) {
        return m_parent->m_children.indexOf(const_cast<TreeItem *>(this));
    }

    return 0;
}

int TreeItem::columnCount() const
{
    return m_data.count();
}

QVariant TreeItem::data(int column) const
{
    return m_data.value(column);
}

void TreeItem::setData(QVariant value, int column)
{
    m_data.replace(column, value);
}

void TreeItem::update()
{
    foreach(TreeItem * child, treeChildren())
    child->update();
}

void TreeItem::apply()
{
    foreach(TreeItem * child, treeChildren())
    child->apply();
}

/*
 * Called after a value has changed to trigger highlighting of tree item.
 */
void TreeItem::setHighlight(bool highlight)
{
    m_changed = false;
    if (m_highlight != highlight) {
        m_highlight = highlight;
        if (highlight) {
            // Add to highlight manager
            if (m_highlightManager->add(this)) {
                // Only emit signal if it was added
                emit updateHighlight(this);
            }
            // Update expiration timeout
            m_highlightExpires = QTime::currentTime().addMSecs(m_highlightTimeMs);
            // start expiration timer if necessary
            m_highlightManager->startTimer(m_highlightExpires);
        } else if (m_highlightManager->remove(this)) {
            // Only emit signal if it was removed
            emit updateHighlight(this);
        }
    }

    // If we have a parent, call recursively to update highlight status of parents.
    // This will ensure that the root of a leaf that is changed also is highlighted.
    // Only updates that really changes values will trigger highlight of parents.
    if (m_parent) {
        m_parent->setHighlight(highlight);
    }
}

void TreeItem::removeHighlight()
{
    m_highlight = false;
    emit updateHighlight(this);
}

void TreeItem::setHighlightManager(HighLightManager *mgr)
{
    m_highlightManager = mgr;
}

QTime TreeItem::getHiglightExpires() const
{
    return m_highlightExpires;
}

QList<MetaObjectTreeItem *> TopTreeItem::getMetaObjectItems()
{
    return m_metaObjectTreeItemsPerObjectIds.values();
}

QVariant ArrayFieldTreeItem::data(int column) const
{
    if (column == 1) {
        if (m_field->getType() == UAVObjectField::UINT8 && m_field->getUnits().toLower() == "char") {
            QString dataString;
            dataString.reserve(2 + m_field->getNumElements());
            dataString.append("'");
            for (uint i = 0; i < m_field->getNumElements(); ++i) {
                dataString.append(m_field->getValue(i).toChar());
            }
            dataString.append("'");
            return dataString;
        } else if (m_field->getUnits().toLower() == "hex") {
            QString dataString;
            int len = TreeItem::maxHexStringLength(m_field->getType());
            QChar fillChar('0');
            dataString.reserve(2 + (len + 1) * m_field->getNumElements());
            dataString.append("{");
            for (uint i = 0; i < m_field->getNumElements(); ++i) {
                if (i > 0) {
                    dataString.append(' ');
                }
                bool ok;
                uint value  = m_field->getValue(i).toUInt(&ok);
                QString str = QString("%1").arg(value, len, 16, fillChar);
                str = str.toUpper();
                dataString.append(str);
            }
            dataString.append("}");
            return dataString;
        }
    }
    return TreeItem::data(column);
}

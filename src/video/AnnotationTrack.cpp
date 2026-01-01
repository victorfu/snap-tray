#include "video/AnnotationTrack.h"
#include <QJsonArray>
#include <algorithm>

AnnotationTrack::AnnotationTrack(QObject *parent)
    : QObject(parent)
    , m_undoStack(new QUndoStack(this))
{
    connect(m_undoStack, &QUndoStack::indexChanged, this, &AnnotationTrack::trackModified);
}

AnnotationTrack::~AnnotationTrack() = default;

void AnnotationTrack::addAnnotation(const VideoAnnotation &annotation)
{
    m_undoStack->push(new AddAnnotationCommand(this, annotation));
}

void AnnotationTrack::removeAnnotation(const QString &id)
{
    const VideoAnnotation *ann = annotation(id);
    if (ann) {
        m_undoStack->push(new RemoveAnnotationCommand(this, id));
    }
}

void AnnotationTrack::updateAnnotation(const VideoAnnotation &annotation)
{
    const VideoAnnotation *existing = this->annotation(annotation.id);
    if (existing) {
        m_undoStack->push(new UpdateAnnotationCommand(this, *existing, annotation));
    }
}

void AnnotationTrack::clear()
{
    m_undoStack->beginMacro("Clear all annotations");
    while (!m_annotations.isEmpty()) {
        m_undoStack->push(new RemoveAnnotationCommand(this, m_annotations.first().id));
    }
    m_undoStack->endMacro();
}

VideoAnnotation *AnnotationTrack::annotation(const QString &id)
{
    for (auto &ann : m_annotations) {
        if (ann.id == id) {
            return &ann;
        }
    }
    return nullptr;
}

const VideoAnnotation *AnnotationTrack::annotation(const QString &id) const
{
    for (const auto &ann : m_annotations) {
        if (ann.id == id) {
            return &ann;
        }
    }
    return nullptr;
}

QList<VideoAnnotation> AnnotationTrack::allAnnotations() const
{
    return m_annotations;
}

QList<VideoAnnotation> AnnotationTrack::annotationsAt(qint64 timeMs, qint64 videoDurationMs) const
{
    QList<VideoAnnotation> result;
    for (const auto &ann : m_annotations) {
        if (ann.isVisibleAt(timeMs, videoDurationMs)) {
            result.append(ann);
        }
    }

    // Sort by zOrder
    std::sort(result.begin(), result.end(),
              [](const VideoAnnotation &a, const VideoAnnotation &b) {
                  return a.zOrder < b.zOrder;
              });

    return result;
}

int AnnotationTrack::count() const
{
    return m_annotations.size();
}

bool AnnotationTrack::isEmpty() const
{
    return m_annotations.isEmpty();
}

QString AnnotationTrack::selectedId() const
{
    return m_selectedId;
}

void AnnotationTrack::setSelectedId(const QString &id)
{
    if (m_selectedId != id) {
        m_selectedId = id;
        emit selectionChanged(id);
    }
}

void AnnotationTrack::clearSelection()
{
    setSelectedId(QString());
}

VideoAnnotation *AnnotationTrack::selectedAnnotation()
{
    return annotation(m_selectedId);
}

const VideoAnnotation *AnnotationTrack::selectedAnnotation() const
{
    return annotation(m_selectedId);
}

void AnnotationTrack::bringToFront(const QString &id)
{
    VideoAnnotation *ann = annotation(id);
    if (!ann) {
        return;
    }

    int maxZ = 0;
    for (const auto &a : m_annotations) {
        maxZ = qMax(maxZ, a.zOrder);
    }

    if (ann->zOrder < maxZ) {
        VideoAnnotation updated = *ann;
        updated.zOrder = maxZ + 1;
        updateAnnotation(updated);
    }
}

void AnnotationTrack::sendToBack(const QString &id)
{
    VideoAnnotation *ann = annotation(id);
    if (!ann) {
        return;
    }

    int minZ = 0;
    for (const auto &a : m_annotations) {
        minZ = qMin(minZ, a.zOrder);
    }

    if (ann->zOrder > minZ) {
        VideoAnnotation updated = *ann;
        updated.zOrder = minZ - 1;
        updateAnnotation(updated);
    }
}

void AnnotationTrack::moveUp(const QString &id)
{
    VideoAnnotation *ann = annotation(id);
    if (!ann) {
        return;
    }

    // Find the annotation with the next higher zOrder
    int currentZ = ann->zOrder;
    int nextZ = INT_MAX;
    QString nextId;

    for (const auto &a : m_annotations) {
        if (a.zOrder > currentZ && a.zOrder < nextZ) {
            nextZ = a.zOrder;
            nextId = a.id;
        }
    }

    if (!nextId.isEmpty()) {
        // Swap z-orders
        VideoAnnotation updated = *ann;
        updated.zOrder = nextZ;

        VideoAnnotation *other = annotation(nextId);
        VideoAnnotation otherUpdated = *other;
        otherUpdated.zOrder = currentZ;

        m_undoStack->beginMacro("Move up");
        updateAnnotation(updated);
        updateAnnotation(otherUpdated);
        m_undoStack->endMacro();
    }
}

void AnnotationTrack::moveDown(const QString &id)
{
    VideoAnnotation *ann = annotation(id);
    if (!ann) {
        return;
    }

    // Find the annotation with the next lower zOrder
    int currentZ = ann->zOrder;
    int prevZ = INT_MIN;
    QString prevId;

    for (const auto &a : m_annotations) {
        if (a.zOrder < currentZ && a.zOrder > prevZ) {
            prevZ = a.zOrder;
            prevId = a.id;
        }
    }

    if (!prevId.isEmpty()) {
        // Swap z-orders
        VideoAnnotation updated = *ann;
        updated.zOrder = prevZ;

        VideoAnnotation *other = annotation(prevId);
        VideoAnnotation otherUpdated = *other;
        otherUpdated.zOrder = currentZ;

        m_undoStack->beginMacro("Move down");
        updateAnnotation(updated);
        updateAnnotation(otherUpdated);
        m_undoStack->endMacro();
    }
}

QUndoStack *AnnotationTrack::undoStack() const
{
    return m_undoStack;
}

void AnnotationTrack::undo()
{
    m_undoStack->undo();
}

void AnnotationTrack::redo()
{
    m_undoStack->redo();
}

bool AnnotationTrack::canUndo() const
{
    return m_undoStack->canUndo();
}

bool AnnotationTrack::canRedo() const
{
    return m_undoStack->canRedo();
}

QJsonObject AnnotationTrack::toJson() const
{
    QJsonObject obj;
    QJsonArray annArray;

    for (const auto &ann : m_annotations) {
        annArray.append(ann.toJson());
    }

    obj["annotations"] = annArray;
    obj["version"] = 1;

    return obj;
}

void AnnotationTrack::fromJson(const QJsonObject &json)
{
    m_annotations.clear();
    m_selectedId.clear();
    m_undoStack->clear();

    QJsonArray annArray = json["annotations"].toArray();
    for (const auto &annVal : annArray) {
        m_annotations.append(VideoAnnotation::fromJson(annVal.toObject()));
    }

    recalculateZOrder();
    emit trackModified();
}

QByteArray AnnotationTrack::toJsonData() const
{
    QJsonDocument doc(toJson());
    return doc.toJson(QJsonDocument::Indented);
}

bool AnnotationTrack::fromJsonData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    fromJson(doc.object());
    return true;
}

void AnnotationTrack::setAnnotationTiming(const QString &id, qint64 startTimeMs, qint64 endTimeMs)
{
    VideoAnnotation *ann = annotation(id);
    if (ann) {
        VideoAnnotation updated = *ann;
        updated.startTimeMs = startTimeMs;
        updated.endTimeMs = endTimeMs;
        updateAnnotation(updated);
    }
}

void AnnotationTrack::offsetAllAnnotations(qint64 offsetMs)
{
    if (m_annotations.isEmpty()) {
        return;
    }

    m_undoStack->beginMacro("Offset all annotations");
    for (const auto &ann : m_annotations) {
        VideoAnnotation updated = ann;
        updated.startTimeMs = qMax(0LL, ann.startTimeMs + offsetMs);
        if (ann.endTimeMs >= 0) {
            updated.endTimeMs = qMax(updated.startTimeMs, ann.endTimeMs + offsetMs);
        }
        updateAnnotation(updated);
    }
    m_undoStack->endMacro();
}

void AnnotationTrack::recalculateZOrder()
{
    // Sort by current zOrder and reassign sequential values
    std::sort(m_annotations.begin(), m_annotations.end(),
              [](const VideoAnnotation &a, const VideoAnnotation &b) {
                  return a.zOrder < b.zOrder;
              });

    for (int i = 0; i < m_annotations.size(); ++i) {
        m_annotations[i].zOrder = i;
    }
}

int AnnotationTrack::findIndex(const QString &id) const
{
    for (int i = 0; i < m_annotations.size(); ++i) {
        if (m_annotations[i].id == id) {
            return i;
        }
    }
    return -1;
}

// AddAnnotationCommand implementation
AddAnnotationCommand::AddAnnotationCommand(AnnotationTrack *track,
                                           const VideoAnnotation &annotation,
                                           QUndoCommand *parent)
    : QUndoCommand("Add annotation", parent)
    , m_track(track)
    , m_annotation(annotation)
{
    if (m_annotation.id.isEmpty()) {
        m_annotation.id = VideoAnnotation::generateId();
    }
}

void AddAnnotationCommand::undo()
{
    int index = m_track->findIndex(m_annotation.id);
    if (index >= 0) {
        m_track->m_annotations.removeAt(index);
        if (m_track->m_selectedId == m_annotation.id) {
            m_track->clearSelection();
        }
        emit m_track->annotationRemoved(m_annotation.id);
    }
}

void AddAnnotationCommand::redo()
{
    m_track->m_annotations.append(m_annotation);
    emit m_track->annotationAdded(m_annotation.id);
}

// RemoveAnnotationCommand implementation
RemoveAnnotationCommand::RemoveAnnotationCommand(AnnotationTrack *track, const QString &id,
                                                 QUndoCommand *parent)
    : QUndoCommand("Remove annotation", parent)
    , m_track(track)
    , m_id(id)
{
    const VideoAnnotation *ann = track->annotation(id);
    if (ann) {
        m_annotation = *ann;
    }
}

void RemoveAnnotationCommand::undo()
{
    m_track->m_annotations.append(m_annotation);
    emit m_track->annotationAdded(m_annotation.id);
}

void RemoveAnnotationCommand::redo()
{
    int index = m_track->findIndex(m_id);
    if (index >= 0) {
        m_track->m_annotations.removeAt(index);
        if (m_track->m_selectedId == m_id) {
            m_track->clearSelection();
        }
        emit m_track->annotationRemoved(m_id);
    }
}

// UpdateAnnotationCommand implementation
UpdateAnnotationCommand::UpdateAnnotationCommand(AnnotationTrack *track,
                                                 const VideoAnnotation &oldAnnotation,
                                                 const VideoAnnotation &newAnnotation,
                                                 QUndoCommand *parent)
    : QUndoCommand("Update annotation", parent)
    , m_track(track)
    , m_oldAnnotation(oldAnnotation)
    , m_newAnnotation(newAnnotation)
{
}

void UpdateAnnotationCommand::undo()
{
    int index = m_track->findIndex(m_oldAnnotation.id);
    if (index >= 0) {
        m_track->m_annotations[index] = m_oldAnnotation;
        emit m_track->annotationUpdated(m_oldAnnotation.id);
    }
}

void UpdateAnnotationCommand::redo()
{
    int index = m_track->findIndex(m_newAnnotation.id);
    if (index >= 0) {
        m_track->m_annotations[index] = m_newAnnotation;
        emit m_track->annotationUpdated(m_newAnnotation.id);
    }
}

int UpdateAnnotationCommand::id() const
{
    // Use annotation ID hash for merging similar updates
    return qHash(m_newAnnotation.id) % 1000;
}

bool UpdateAnnotationCommand::mergeWith(const QUndoCommand *other)
{
    const UpdateAnnotationCommand *otherCmd = dynamic_cast<const UpdateAnnotationCommand *>(other);
    if (!otherCmd) {
        return false;
    }

    // Only merge if same annotation
    if (otherCmd->m_newAnnotation.id != m_newAnnotation.id) {
        return false;
    }

    // Keep our old state, take their new state
    m_newAnnotation = otherCmd->m_newAnnotation;
    return true;
}

#ifndef ANNOTATION_TRACK_H
#define ANNOTATION_TRACK_H

#include "video/VideoAnnotation.h"
#include <QJsonDocument>
#include <QList>
#include <QObject>
#include <QUndoStack>

class AnnotationTrack : public QObject {
    Q_OBJECT

public:
    explicit AnnotationTrack(QObject *parent = nullptr);
    ~AnnotationTrack() override;

    // Annotation management
    void addAnnotation(const VideoAnnotation &annotation);
    void removeAnnotation(const QString &id);
    void updateAnnotation(const VideoAnnotation &annotation);
    void clear();

    // Queries
    VideoAnnotation *annotation(const QString &id);
    const VideoAnnotation *annotation(const QString &id) const;
    QList<VideoAnnotation> allAnnotations() const;
    QList<VideoAnnotation> annotationsAt(qint64 timeMs, qint64 videoDurationMs) const;
    int count() const;
    bool isEmpty() const;

    // Selection
    QString selectedId() const;
    void setSelectedId(const QString &id);
    void clearSelection();
    VideoAnnotation *selectedAnnotation();
    const VideoAnnotation *selectedAnnotation() const;

    // Z-order management
    void bringToFront(const QString &id);
    void sendToBack(const QString &id);
    void moveUp(const QString &id);
    void moveDown(const QString &id);

    // Undo/Redo
    QUndoStack *undoStack() const;
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);
    QByteArray toJsonData() const;
    bool fromJsonData(const QByteArray &data);

    // Timing adjustment
    void setAnnotationTiming(const QString &id, qint64 startTimeMs, qint64 endTimeMs);
    void offsetAllAnnotations(qint64 offsetMs);

signals:
    void annotationAdded(const QString &id);
    void annotationRemoved(const QString &id);
    void annotationUpdated(const QString &id);
    void selectionChanged(const QString &id);
    void trackModified();

private:
    friend class AddAnnotationCommand;
    friend class RemoveAnnotationCommand;
    friend class UpdateAnnotationCommand;

    void recalculateZOrder();
    int findIndex(const QString &id) const;

    QList<VideoAnnotation> m_annotations;
    QString m_selectedId;
    QUndoStack *m_undoStack;
};

// Undo commands
class AddAnnotationCommand : public QUndoCommand {
public:
    AddAnnotationCommand(AnnotationTrack *track, const VideoAnnotation &annotation,
                         QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    AnnotationTrack *m_track;
    VideoAnnotation m_annotation;
};

class RemoveAnnotationCommand : public QUndoCommand {
public:
    RemoveAnnotationCommand(AnnotationTrack *track, const QString &id,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    AnnotationTrack *m_track;
    VideoAnnotation m_annotation;
    QString m_id;
};

class UpdateAnnotationCommand : public QUndoCommand {
public:
    UpdateAnnotationCommand(AnnotationTrack *track, const VideoAnnotation &oldAnnotation,
                            const VideoAnnotation &newAnnotation,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

private:
    AnnotationTrack *m_track;
    VideoAnnotation m_oldAnnotation;
    VideoAnnotation m_newAnnotation;
};

#endif // ANNOTATION_TRACK_H

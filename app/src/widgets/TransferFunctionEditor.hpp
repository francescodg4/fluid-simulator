#pragma once

#include <fluid/TransferFunction.hpp>

#include <QGraphicsView>
#include <QVector>

class QGraphicsPathItem;

namespace fluid::app {

class ControlPointItem;
class TransferBackgroundItem;

/**
 * Interactive transfer function editor built on QGraphicsView.
 * 
 * The opacity curve is a chain of draggable control points drawn over the colormap and a
 * histogram of the current scalar field. Double-click adds a point, right-click (or Delete)
 * removes one; the end points slide vertically only.
 */
class TransferFunctionEditor : public QGraphicsView {
    Q_OBJECT
public:
    explicit TransferFunctionEditor(QWidget* parent = nullptr);
    ~TransferFunctionEditor() override;

    void setTransferFunction(const TransferFunction& tf);
    const TransferFunction& transferFunction() const { return m_tf; }
    /** Histogram of the scalar field over the displayed range (any scale; normalised internally). */
    void setHistogram(const QVector<float>& bins);
    void setRangeLabels(const QString& minimum, const QString& maximum);

    QSize sizeHint() const override { return { 260, 150 }; }

signals:
    void transferFunctionChanged(const fluid::TransferFunction& tf);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    friend class ControlPointItem;
    QRectF plotRect() const;
    QPointF toScene(const TransferFunction::OpacityPoint& p) const;
    TransferFunction::OpacityPoint fromScene(const QPointF& pos) const;
    void rebuildPoints();
    void pointMoved();
    void updateCurve();
    void commit();

    TransferFunction m_tf;
    TransferBackgroundItem* m_background = nullptr;
    QGraphicsPathItem* m_fill = nullptr;
    QGraphicsPathItem* m_curve = nullptr;
    QVector<ControlPointItem*> m_points;
    bool m_syncing = false;
};

} // namespace fluid::app

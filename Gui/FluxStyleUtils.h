#ifndef GUI_FLUX_STYLE_UTILS_H
#define GUI_FLUX_STYLE_UTILS_H

#include <QColor>
#include <QPalette>
#include <QWidget>

namespace FluxStyle {

inline QColor mix(const QColor& a, const QColor& b, qreal t)
{
    const qreal clamped = t < 0. ? 0. : (t > 1. ? 1. : t);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * clamped,
                            a.greenF() + (b.greenF() - a.greenF()) * clamped,
                            a.blueF() + (b.blueF() - a.blueF()) * clamped,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * clamped);
}

inline QColor withAlpha(const QColor& color, qreal alpha)
{
    QColor out(color);
    out.setAlphaF(alpha < 0. ? 0. : (alpha > 1. ? 1. : alpha));
    return out;
}

inline QColor window(const QWidget* widget)
{
    return widget->palette().color(QPalette::Window);
}

inline QColor base(const QWidget* widget)
{
    return widget->palette().color(QPalette::Base);
}

inline QColor button(const QWidget* widget)
{
    return widget->palette().color(QPalette::Button);
}

inline QColor text(const QWidget* widget)
{
    return widget->palette().color(QPalette::Text);
}

inline QColor disabledText(const QWidget* widget)
{
    const QColor disabled = widget->palette().color(QPalette::Disabled, QPalette::Text);
    return disabled.isValid() ? disabled : mix(text(widget), window(widget), 0.5);
}

inline QColor accent(const QWidget* widget)
{
    return widget->palette().color(QPalette::Highlight);
}

inline QColor border(const QWidget* widget)
{
    return mix(button(widget), text(widget), 0.2);
}

inline QColor hoverBorder(const QWidget* widget)
{
    return mix(accent(widget), text(widget), 0.18);
}

inline QColor track(const QWidget* widget)
{
    return mix(base(widget), text(widget), 0.18);
}

inline QColor handle(const QWidget* widget)
{
    return mix(button(widget), accent(widget), 0.38);
}

inline QColor focusOutline(const QWidget* widget)
{
    return accent(widget);
}

inline QColor keyframeColor()
{
    return QColor(226, 171, 67);
}

inline QColor interpolatedColor()
{
    return QColor(94, 184, 118);
}

inline QColor expressionColor()
{
    return QColor(137, 112, 206);
}

inline QColor alteredColor(const QWidget* widget)
{
    return mix(accent(widget), text(widget), 0.28);
}

} // namespace FluxStyle

#endif // GUI_FLUX_STYLE_UTILS_H

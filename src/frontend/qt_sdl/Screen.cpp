/*
    Copyright 2016-2024 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <string.h>

#include <optional>
#include <cmath>

#include <QPaintEvent>
#include <QPainter>
#ifndef _WIN32
#ifndef APPLE
#include <qpa/qplatformnativeinterface.h>
#endif
#endif
#include <QDateTime>

#include "OpenGLSupport.h"
#include "duckstation/gl/context.h"

#include "main.h"
#include "EmuInstance.h"

#include "NDS.h"
#include "GPU.h"
#include "GPU3D_Soft.h"
#include "GPU3D_OpenGL.h"
#include "Platform.h"
#include "Config.h"

#include "main_shaders.h"
#include "OSD_shaders.h"
#include "font.h"

using namespace melonDS;


const u32 kOSDMargin = 6;


ScreenPanel::ScreenPanel(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents);

    QWidget* w = parent;
    for (;;)
    {
        mainWindow = qobject_cast<MainWindow*>(w);
        if (mainWindow) break;
        w = w->parentWidget();
        if (!w) break;
    }

    emuInstance = mainWindow->getEmuInstance();

    mouseHide = false;
    mouseHideDelay = 0;

    QTimer* mouseTimer = setupMouseTimer();
    connect(mouseTimer, &QTimer::timeout, [=] { if (mouseHide) setCursor(Qt::BlankCursor);});

    osdEnabled = false;
    osdID = 1;
    
    loadConfig();
    setFilter(mainWindow->getWindowConfig().GetBool("ScreenFilter"));
}

ScreenPanel::~ScreenPanel()
{
    mouseTimer->stop();
    delete mouseTimer;
}

void ScreenPanel::loadConfig()
{
    auto& cfg = mainWindow->getWindowConfig();
    
    screenRotation = cfg.GetInt("ScreenRotation");
    screenGap = cfg.GetInt("ScreenGap");
    screenLayout = cfg.GetInt("ScreenLayout");
    screenSwap = cfg.GetBool("ScreenSwap");
    screenSizing = cfg.GetInt("ScreenSizing");
    integerScaling = cfg.GetBool("IntegerScaling");
    screenAspectTop = cfg.GetInt("ScreenAspectTop");
    screenAspectBot = cfg.GetInt("ScreenAspectBot");
}

void ScreenPanel::setFilter(bool filter)
{
    this->filter = filter;
}

void ScreenPanel::setMouseHide(bool enable, int delay)
{
    mouseHide = enable;
    mouseHideDelay = delay;

    mouseTimer->setInterval(mouseHideDelay);
}

void ScreenPanel::setupScreenLayout()
{
    int w = width();
    int h = height();

    int sizing = screenSizing;
    if (sizing == screenSizing_Auto) sizing = autoScreenSizing;

    float aspectTop, aspectBot;

    for (auto ratio : aspectRatios)
    {
        if (ratio.id == screenAspectTop)
            aspectTop = ratio.ratio;
        if (ratio.id == screenAspectBot)
            aspectBot = ratio.ratio;
    }

    if (aspectTop == 0)
        aspectTop = ((float) w / h) / (4.f / 3.f);

    if (aspectBot == 0)
        aspectBot = ((float) w / h) / (4.f / 3.f);

    layout.Setup(w, h,
                static_cast<ScreenLayoutType>(screenLayout),
                static_cast<ScreenRotation>(screenRotation),
                static_cast<ScreenSizing>(sizing),
                screenGap,
                integerScaling != 0,
                screenSwap != 0,
                aspectTop,
                aspectBot);

    numScreens = layout.GetScreenTransforms(screenMatrix[0], screenKind);
}

QSize ScreenPanel::screenGetMinSize(int factor = 1)
{
    bool isHori = (screenRotation == screenRot_90Deg
        || screenRotation == screenRot_270Deg);
    int gap = screenGap * factor;

    int w = 256 * factor;
    int h = 192 * factor;

    if (screenSizing == screenSizing_TopOnly
        || screenSizing == screenSizing_BotOnly)
    {
        return QSize(w, h);
    }

    if (screenLayout == screenLayout_Natural)
    {
        if (isHori)
            return QSize(h+gap+h, w);
        else
            return QSize(w, h+gap+h);
    }
    else if (screenLayout == screenLayout_Vertical)
    {
        if (isHori)
            return QSize(h, w+gap+w);
        else
            return QSize(w, h+gap+h);
    }
    else if (screenLayout == screenLayout_Horizontal)
    {
        if (isHori)
            return QSize(h+gap+h, w);
        else
            return QSize(w+gap+w, h);
    }
    else // hybrid
    {
        if (isHori)
            return QSize(h+gap+h, 3*w + (int)ceil((4*gap) / 3.0));
        else
            return QSize(3*w + (int)ceil((4*gap) / 3.0), h+gap+h);
    }
}

void ScreenPanel::onScreenLayoutChanged()
{
    loadConfig();

    setMinimumSize(screenGetMinSize());
    setupScreenLayout();
}

void ScreenPanel::onAutoScreenSizingChanged(int sizing)
{
    autoScreenSizing = sizing;
    if (screenSizing != screenSizing_Auto) return;

    setupScreenLayout();
}

void ScreenPanel::resizeEvent(QResizeEvent* event)
{
    setupScreenLayout();
    QWidget::resizeEvent(event);
}

void ScreenPanel::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (event->button() != Qt::LeftButton) return;

    int x = event->pos().x();
    int y = event->pos().y();

    if (layout.GetTouchCoords(x, y, false))
    {
        touching = true;
        assert(emuInstance->getNDS() != nullptr);
        emuInstance->getNDS()->TouchScreen(x, y);
    }
}

void ScreenPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
    if (event->button() != Qt::LeftButton) return;

    if (touching)
    {
        touching = false;
        assert(emuInstance->getNDS() != nullptr);
        emuInstance->getNDS()->ReleaseScreen();
    }
}

void ScreenPanel::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    showCursor();

    //if (!(event->buttons() & Qt::LeftButton)) return;
    if (!touching) return;

    int x = event->pos().x();
    int y = event->pos().y();

    if (layout.GetTouchCoords(x, y, true))
    {
        assert(emuInstance->getNDS() != nullptr);
        emuInstance->getNDS()->TouchScreen(x, y);
    }
}

void ScreenPanel::tabletEvent(QTabletEvent* event)
{
    event->accept();

    switch(event->type())
    {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
        {
#if QT_VERSION_MAJOR == 6
            int x = event->position().x();
            int y = event->position().y();
#else
            int x = event->x();
            int y = event->y();
#endif

            if (layout.GetTouchCoords(x, y, event->type()==QEvent::TabletMove))
            {
                touching = true;
                assert(emuInstance->getNDS() != nullptr);
                emuInstance->getNDS()->TouchScreen(x, y);
            }
        }
        break;
    case QEvent::TabletRelease:
        if (touching)
        {
            assert(emuInstance->getNDS() != nullptr);
            emuInstance->getNDS()->ReleaseScreen();
            touching = false;
        }
        break;
    default:
        break;
    }
}

void ScreenPanel::touchEvent(QTouchEvent* event)
{
#if QT_VERSION_MAJOR == 6
    if (event->device()->type() == QInputDevice::DeviceType::TouchPad)
        return;
#endif

    event->accept();

    switch(event->type())
    {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
#if QT_VERSION_MAJOR == 6
        if (event->points().length() > 0)
        {
            QPointF lastPosition = event->points().first().lastPosition();
#else
        if (event->touchPoints().length() > 0)
        {
            QPointF lastPosition = event->touchPoints().first().lastPos();
#endif
            int x = (int)lastPosition.x();
            int y = (int)lastPosition.y();

            if (layout.GetTouchCoords(x, y, event->type()==QEvent::TouchUpdate))
            {
                touching = true;
                assert(emuInstance->getNDS() != nullptr);
                emuInstance->getNDS()->TouchScreen(x, y);
            }
        }
        break;
    case QEvent::TouchEnd:
        if (touching)
        {
            assert(emuInstance->getNDS() != nullptr);
            emuInstance->getNDS()->ReleaseScreen();
            touching = false;
        }
        break;
    default:
        break;
    }
}

bool ScreenPanel::event(QEvent* event)
{
    if (event->type() == QEvent::TouchBegin
        || event->type() == QEvent::TouchEnd
        || event->type() == QEvent::TouchUpdate)
    {
        touchEvent((QTouchEvent*)event);
        return true;
    }

    return QWidget::event(event);
}

void ScreenPanel::showCursor()
{
    mainWindow->panel->setCursor(Qt::ArrowCursor);
    mouseTimer->start();
}

QTimer* ScreenPanel::setupMouseTimer()
{
    mouseTimer = new QTimer();
    mouseTimer->setSingleShot(true);
    mouseTimer->setInterval(mouseHideDelay);
    mouseTimer->start();

    return mouseTimer;
}

int ScreenPanel::osdFindBreakPoint(const char* text, int i)
{
    // i = character that went out of bounds

    for (int j = i; j >= 0; j--)
    {
        if (text[j] == ' ')
            return j;
    }

    return i;
}

void ScreenPanel::osdLayoutText(const char* text, int* width, int* height, int* breaks)
{
    int w = 0;
    int h = 14;
    int totalw = 0;
    int maxw = ((QWidget*)this)->width() - (kOSDMargin*2);
    int lastbreak = -1;
    int numbrk = 0;
    u16* ptr;

    memset(breaks, 0, sizeof(int)*64);

    for (int i = 0; text[i] != '\0'; )
	{
	    int glyphsize;
		if (text[i] == ' ')
		{
			glyphsize = 6;
		}
		else
        {
            u32 ch = text[i];
            if (ch < 0x10 || ch > 0x7E) ch = 0x7F;

            ptr = &::font[(ch-0x10) << 4];
            glyphsize = ptr[0];
            if (!glyphsize) glyphsize = 6;
            else            glyphsize += 2; // space around the character
        }

		w += glyphsize;
		if (w > maxw)
        {
            // wrap shit as needed
            if (text[i] == ' ')
            {
                if (numbrk >= 64) break;
                breaks[numbrk++] = i;
                i++;
            }
            else
            {
                int brk = osdFindBreakPoint(text, i);
                if (brk != lastbreak) i = brk;

                if (numbrk >= 64) break;
                breaks[numbrk++] = i;

                lastbreak = brk;
            }

            w = 0;
            h += 14;
        }
        else
            i++;

        if (w > totalw) totalw = w;
    }

    *width = totalw;
    *height = h;
}

unsigned int ScreenPanel::osdRainbowColor(int inc)
{
    // inspired from Acmlmboard

    if      (inc < 100) return 0xFFFF9B9B + (inc << 8);
    else if (inc < 200) return 0xFFFFFF9B - ((inc-100) << 16);
    else if (inc < 300) return 0xFF9BFF9B + (inc-200);
    else if (inc < 400) return 0xFF9BFFFF - ((inc-300) << 8);
    else if (inc < 500) return 0xFF9B9BFF + ((inc-400) << 16);
    else                return 0xFFFF9BFF - (inc-500);
}

void ScreenPanel::osdRenderItem(OSDItem* item)
{
    int w, h;
    int breaks[64];

    char* text = item->text;
    u32 color = item->color;

    bool rainbow = (color == 0);
    u32 ticks = (u32)QDateTime::currentMSecsSinceEpoch();
    u32 rainbowinc = ((text[0] * 17) + (ticks * 13)) % 600;

    color |= 0xFF000000;
    const u32 shadow = 0xE0000000;

    osdLayoutText(text, &w, &h, breaks);

    item->bitmap = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    u32* bitmap = (u32*)item->bitmap.bits();
    memset(bitmap, 0, w*h*sizeof(u32));

    int x = 0, y = 1;
    u32 maxw = ((QWidget*)this)->width() - (kOSDMargin*2);
    int curline = 0;
    u16* ptr;

    for (int i = 0; text[i] != '\0'; )
	{
	    int glyphsize;
		if (text[i] == ' ')
		{
			x += 6;
		}
		else
        {
            u32 ch = text[i];
            if (ch < 0x10 || ch > 0x7E) ch = 0x7F;

            ptr = &::font[(ch-0x10) << 4];
            int glyphsize = ptr[0];
            if (!glyphsize) x += 6;
            else
            {
                x++;

                if (rainbow)
                {
                    color = osdRainbowColor(rainbowinc);
                    rainbowinc = (rainbowinc + 30) % 600;
                }

                // draw character
                for (int cy = 0; cy < 12; cy++)
                {
                    u16 val = ptr[4+cy];

                    for (int cx = 0; cx < glyphsize; cx++)
                    {
                        if (val & (1<<cx))
                            bitmap[((y+cy) * w) + x+cx] = color;
                    }
                }

                x += glyphsize;
                x++;
            }
        }

		i++;
		if (breaks[curline] && i >= breaks[curline])
        {
            i = breaks[curline++];
            if (text[i] == ' ') i++;

            x = 0;
            y += 14;
        }
    }

    // shadow
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            u32 val;

            val = bitmap[(y * w) + x];
            if ((val >> 24) == 0xFF) continue;

            if (x > 0)   val  = bitmap[(y * w) + x-1];
            if (x < w-1) val |= bitmap[(y * w) + x+1];
            if (y > 0)
            {
                if (x > 0)   val |= bitmap[((y-1) * w) + x-1];
                val |= bitmap[((y-1) * w) + x];
                if (x < w-1) val |= bitmap[((y-1) * w) + x+1];
            }
            if (y < h-1)
            {
                if (x > 0)   val |= bitmap[((y+1) * w) + x-1];
                val |= bitmap[((y+1) * w) + x];
                if (x < w-1) val |= bitmap[((y+1) * w) + x+1];
            }

            if ((val >> 24) == 0xFF)
                bitmap[(y * w) + x] = shadow;
        }
    }
}

void ScreenPanel::osdDeleteItem(OSDItem* item)
{
}

void ScreenPanel::osdSetEnabled(bool enabled)
{
    osdMutex.lock();
    osdEnabled = enabled;
    osdMutex.unlock();
}

void ScreenPanel::osdAddMessage(unsigned int color, const char* text)
{
    if (!osdEnabled) return;

    osdMutex.lock();

    OSDItem item;

    item.id = osdID++;
    item.timestamp = QDateTime::currentMSecsSinceEpoch();
    strncpy(item.text, text, 255); item.text[255] = '\0';
    item.color = color;
    item.rendered = false;

    osdItems.push_back(item);

    osdMutex.unlock();
}

void ScreenPanel::osdUpdate()
{
    osdMutex.lock();

    qint64 tick_now = QDateTime::currentMSecsSinceEpoch();
    qint64 tick_min = tick_now - 2500;

    for (auto it = osdItems.begin(); it != osdItems.end(); )
    {
        OSDItem& item = *it;

        if ((!osdEnabled) || (item.timestamp < tick_min))
        {
            osdDeleteItem(&item);
            it = osdItems.erase(it);
            continue;
        }

        if (!item.rendered)
        {
            osdRenderItem(&item);
            item.rendered = true;
        }

        it++;
    }

    osdMutex.unlock();
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    // "ui" contains members for each UI element that you added in
    // design mode. Look what the name of the list view is that you
    // added there, and adapt accordingly
    ui->listView->setModel(&model);

    // if you want, you can initialize your model with some items:
    model.setStringList({"Bananas", "DVD Player"});
}


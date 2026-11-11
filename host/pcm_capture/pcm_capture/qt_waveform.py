"""Bounded min/max dual-channel preview, no data acquisition in UI."""
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor,QPainter,QPen
from PySide6.QtWidgets import QWidget

class WaveformWidget(QWidget):
    def __init__(self,parent=None):
        super().__init__(parent)
        self.samples=();self.samples_right=();self.sample_rate=0;self.sample_bits=16
        self.setMinimumHeight(210)

    def set_samples(self,samples,sample_rate,right=None,sample_bits=16):
        self.samples=tuple(samples[-4096:]);self.samples_right=tuple((right or [])[-4096:])
        self.sample_rate=sample_rate or 0;self.sample_bits=sample_bits;self.update()

    def paintEvent(self,event):
        painter=QPainter(self);painter.fillRect(self.rect(),QColor('#101b2d'))
        if not self.samples:
            painter.setPen(QColor('#cedbed'));painter.drawText(self.rect(),Qt.AlignmentFlag.AlignCenter,'Waiting for valid PCM frames');return
        channels=[('L',self.samples,'#44d8bf')]
        if self.samples_right:channels.append(('R',self.samples_right,'#efbb5c'))
        width=max(self.width()-20,1);lane=self.height()/len(channels)
        scale=float(1<<(self.sample_bits-1))
        duration=len(self.samples)/self.sample_rate*1000 if self.sample_rate else 0
        for index,(name,samples,color) in enumerate(channels):
            top=index*lane;height=max(lane-32,1);mid=top+24+height/2
            painter.setPen(QColor('#cedbed'));painter.drawText(12,int(top+17),f'{name} | signed{self.sample_bits} LE | last {duration:.1f} ms')
            painter.setPen(QPen(QColor('#304059'),1));painter.drawLine(10,int(mid),width+10,int(mid))
            painter.setPen(QPen(QColor(color),1));count=len(samples);columns=min(width,count)
            for x in range(columns):
                a=x*count//columns;b=max(a+1,(x+1)*count//columns);values=samples[a:b]
                px=10+x*width//columns
                painter.drawLine(px,int(mid-max(values)/scale*height/2),px,int(mid-min(values)/scale*height/2))

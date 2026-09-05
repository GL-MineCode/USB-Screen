# -*- coding: utf-8 -*-
# =============================================================================
#  MSU2_MINI_DemoV1.6 源码恢复文件
# -----------------------------------------------------------------------------
#  本文件由 PyInstaller 打包的 MSU2_MINI_DemoV1.6.exe 逆向恢复而来。
#
#  恢复流程:
#    1. pyinstxtractor-ng 解包 exe        -> .pyc (Python 3.11, magic 0xA70D0D0A)
#    2. Python 3.11 提取全部嵌套 code 对象 -> 逐函数 .pyc
#    3. pycdc (Decompyle++) 逐函数反编译
#    4. PyLingual 在线反编译交叉验证
#    5. 以 pycdas 反汇编字节码为"真值"人工修复结构/运算错误
#
#  说明:
#    - 变量名、常量、函数名均来自字节码, 与原始源码一致
#    - 反编译工具对 Python 3.11 的部分运算(// >> << % | 等)解析有误,
#      已根据字节码逐一修正
#    - 本文件为逆向研究用途, 请遵守相关法律法规及软件许可
# =============================================================================

import serial
import serial.tools.list_ports
import time
import threading
import psutil
import os
import pyautogui
from datetime import datetime
from PIL import Image
import sys

from PyQt6.QtWidgets import (
    QApplication, QWidget, QPushButton, QLabel,
    QSlider, QPlainTextEdit, QFileDialog,
)
from PyQt6.QtCore import Qt, QObject, pyqtSignal


class MSN_Device:
    def __init__(self, com, version):
        self.com = com
        self.version = version
        self.name = 'MSN'
        self.baud_rate = 19200


My_MSN_Device = []


class MSN_Data:
    def __init__(self, name, unit, family, data):
        self.name = name
        self.unit = unit
        self.family = family
        self.data = data


My_MSN_Data = []

RED = 63488
GREEN = 2016
BLUE = 31
WHITE = 65535
BLACK = 0
YELLOW = 65504
GRAY0 = 61309
GRAY1 = 33808
GRAY2 = 16904

hex_code = b''
G_screnn0 = bytearray()
G_screnn1 = bytearray()
Img_data_use = bytearray()
G_screnn0_OK = 0
G_screnn1_OK = 0
size_USE_X1 = 0
size_USE_Y1 = 0
Show_W = 500
Show_H = 350


# ---------------------------------------------------------------------------
# GUI 线程桥接 (PyQt 要求 UI 只能在主线程操作, 通过信号跨线程更新)
# ---------------------------------------------------------------------------
class GUI_Bridge(QObject):
    label1_text = pyqtSignal(str)
    label1_bg = pyqtSignal(str)
    label2_bg = pyqtSignal(str)
    label3_text = pyqtSignal(str)
    label4_text = pyqtSignal(str)
    label5_text = pyqtSignal(str)
    label6_text = pyqtSignal(str)
    text1_clear = pyqtSignal()
    text1_append = pyqtSignal(str)


bridge = None
label2_bg_cache = None
thread1_stop = 0
ser = None


def GUI_Label1_SetText(s):
    if bridge is not None:
        bridge.label1_text.emit(s)


def GUI_Label1_SetBG(c):
    if bridge is not None:
        bridge.label1_bg.emit(c)


def GUI_Label2_SetBG(c):
    global label2_bg_cache
    if bridge is not None and c != label2_bg_cache:
        label2_bg_cache = c
        bridge.label2_bg.emit(c)


def GUI_Text_Clear():
    if bridge is not None:
        bridge.text1_clear.emit()


def GUI_Text_Append(s):
    if bridge is not None:
        bridge.text1_append.emit(s)


def _set_S1(v):
    global S1_val
    S1_val = v


def _set_S2(v):
    global S2_val
    S2_val = v


def _set_S3(v):
    global S3_val
    S3_val = v


def _label1_set_bg(c):
    global Label1
    Label1.setStyleSheet('background-color: ' + c + ';')


def _label2_set_bg(c):
    global Label2
    Label2.setStyleSheet('background-color: ' + c + ';')


def _text1_clear():
    global Text1
    Text1.clear()


def _text1_append(s):
    global Text1
    Text1.appendPlainText(s.rstrip('\n'))


# ---------------------------------------------------------------------------
# 文件选择 / 图像转换
# ---------------------------------------------------------------------------
def Get_Photo_Path1():
    global photo_path1
    photo_path1, _ = QFileDialog.getOpenFileName(
        None, '选择文件', '', 'Image file (*.jpg *.jpeg *.png *.bmp)')
    if photo_path1:
        bridge.label3_text.emit(photo_path1[-20:])


def Get_Photo_Path2():
    global photo_path2
    photo_path2, _ = QFileDialog.getOpenFileName(None, '选择文件', '', 'Bin file (*.bin)')
    if photo_path2:
        bridge.label4_text.emit(photo_path2[-20:])
        photo_path2 = photo_path2[:-4]


def Get_Photo_Path3():
    global photo_path3
    photo_path3, _ = QFileDialog.getOpenFileName(
        None, '选择文件', '', 'Image file (*.jpg *.jpeg *.png *.bmp)')
    if photo_path3:
        bridge.label5_text.emit(photo_path3[-20:])


def Get_Photo_Path4():
    global photo_path4
    photo_path4, _ = QFileDialog.getOpenFileName(
        None, '选择文件', '', 'Image file (*.jpg *.jpeg *.png *.bmp)')
    if photo_path4:
        bridge.label6_text.emit(photo_path4[-20:])


def Writet_Photo_Path1():
    """将所选图片转换为 RGB565 数据并写入 Flash (地址 3826)。"""
    global write_path1, Img_data_use
    if write_path1 == 0:
        GUI_Text_Clear()
        GUI_Text_Append('图像格式转换...\n')
        im1 = Image.open(photo_path1)
        if im1.width >= im1.height * 2:
            im2 = im1.resize((int(80 * im1.width / im1.height), 80))
            Img_m = int(im2.width / 2)
            box = (Img_m - 80, 0, Img_m + 80, 80)
            im2 = im2.crop(box)
        else:
            im2 = im1.resize((160, int(160 * im1.height / im1.width)))
            Img_m = int(im2.height / 2)
            box = (0, Img_m - 40, 160, Img_m + 40)
            im2 = im2.crop(box)
        im2 = im2.convert('RGB')
        Img_data_use = bytearray()
        for y in range(0, 80):
            for x in range(0, 160):
                (r, g, b) = im2.getpixel((x, y))
                Img_data_use.append((r >> 3) << 3 | g >> 5)
                Img_data_use.append((g % 32 >> 2) << 5 | b >> 3)
        write_path1 = 1


def Writet_Photo_Path2():
    """准备烧写 Flash 固件。"""
    global write_path2
    if write_path2 == 0:
        write_path2 = 1
        GUI_Text_Clear()
        GUI_Text_Append('准备烧写Flash固件...\n')


def Writet_Photo_Path3():
    """将所选图片转换为 RGB565 数据并写入 Flash (地址 3926)。"""
    global write_path3, Img_data_use
    if write_path3 == 0:
        GUI_Text_Clear()
        GUI_Text_Append('图像格式转换...\n')
        im1 = Image.open(photo_path3)
        if im1.width >= im1.height * 2:
            im2 = im1.resize((int(80 * im1.width / im1.height), 80))
            Img_m = int(im2.width / 2)
            box = (Img_m - 80, 0, Img_m + 80, 80)
            im2 = im2.crop(box)
        else:
            im2 = im1.resize((160, int(160 * im1.height / im1.width)))
            Img_m = int(im2.height / 2)
            box = (0, Img_m - 40, 160, Img_m + 40)
            im2 = im2.crop(box)
        im2 = im2.convert('RGB')
        Img_data_use = bytearray()
        for y in range(0, 80):
            for x in range(0, 160):
                (r, g, b) = im2.getpixel((x, y))
                Img_data_use.append((r >> 3) << 3 | g >> 5)
                Img_data_use.append((g % 32 >> 2) << 5 | b >> 3)
        write_path3 = 1


def Writet_Photo_Path4():
    """将 0..35 号动图帧序列转换为 RGB565 数据 (每帧 160x80)。"""
    global write_path4, Img_data_use
    if write_path4 == 0:
        GUI_Text_Clear()
        GUI_Text_Append('动图格式转换中...\n')
        time.sleep(0.1)
        Path_use = photo_path4
        if Path_use[-4] == '.':
            write_path4 = Path_use[-4:]
            Path_use = Path_use[:-5]
        elif Path_use[-5] == '.':
            write_path4 = Path_use[-5:]
            Path_use = Path_use[:-6]
        else:
            GUI_Text_Append('动图名称不符合要求！\n')
        Img_data_use = bytearray()
        u_time = time.time()
        for i in range(0, 36):
            im1 = Image.open(Path_use + str(i) + write_path4)
            if im1.width >= im1.height * 2:
                im2 = im1.resize((int(80 * im1.width / im1.height), 80))
                Img_m = int(im2.width / 2)
                box = (Img_m - 80, 0, Img_m + 80, 80)
                im2 = im2.crop(box)
            else:
                im2 = im1.resize((160, int(160 * im1.height / im1.width)))
                Img_m = int(im2.height / 2)
                box = (0, Img_m - 40, 160, Img_m + 40)
                im2 = im2.crop(box)
            im2 = im2.convert('RGB')
            for y in range(0, 80):
                for x in range(0, 160):
                    (r, g, b) = im2.getpixel((x, y))
                    Img_data_use.append((r >> 3) << 3 | g >> 5)
                    Img_data_use.append((g % 32 >> 2) << 5 | b >> 3)
        u_time = time.time() - u_time
        u_time = int(u_time * 1000)
        GUI_Text_Append('转换完成,耗时' + str(u_time) + 'ms\n')
        write_path4 = 1


# ---------------------------------------------------------------------------
# 页面切换 / 显示方向
# ---------------------------------------------------------------------------
def Page_UP():
    global State_machine, State_change
    State_machine = State_machine + 1
    State_change = 1
    if State_machine > 5:
        State_machine = 0


def Page_Down():
    global State_machine, State_change
    State_machine = State_machine - 1
    State_change = 1
    if State_machine < 0:
        State_machine = 5


def LCD_Change():
    global LCD_Change_use
    LCD_Change_use = LCD_Change_use + 1
    if LCD_Change_use > 1:
        LCD_Change_use = 0


# ---------------------------------------------------------------------------
# 串口读写
# ---------------------------------------------------------------------------
def SER_Write(Data_U0):
    global Device_State
    try:
        if not ser.is_open:
            Device_State = 0
        ser.write(Data_U0)
    except:
        Device_State = 0
        try:
            ser.close()
        except:
            pass


def SER_Read():
    global Device_State
    try:
        Data_U1 = ser.read(ser.in_waiting)
        return Data_U1
    except:
        Device_State = 0
        try:
            ser.close()
        except:
            pass
        return 0


# ---------------------------------------------------------------------------
# 单片机寄存器 / 数据读写 (通信协议)
#   命令帧: [头, 命令, 地址/类型, ...]
#   Read_M_u8 / Read_M_u16 / Write_M_u8 / Write_M_u16 / Read_ADC_CH
# ---------------------------------------------------------------------------
def Read_M_u8(add):
    hex_use = bytearray()
    hex_use.append(0)
    hex_use.append(48)
    hex_use.append(0)
    hex_use.append(add // 256)
    hex_use.append(add % 256)
    hex_use.append(0)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return recv[5]


def Read_M_u16(add):
    hex_use = bytearray()
    hex_use.append(0)
    hex_use.append(48)
    hex_use.append(32)
    hex_use.append(add % 256)
    hex_use.append(0)
    hex_use.append(0)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return recv[4] * 256 + recv[5]


def Write_M_u8(add, data_w):
    hex_use = bytearray()
    hex_use.append(0)
    hex_use.append(48)
    hex_use.append(128)
    hex_use.append(add // 256)
    hex_use.append(add % 256)
    hex_use.append(data_w % 256)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return


def Write_M_u16(add, data_w):
    hex_use = bytearray()
    hex_use.append(0)
    hex_use.append(48)
    hex_use.append(32)
    hex_use.append(add % 256)
    hex_use.append(data_w // 256)
    hex_use.append(data_w % 256)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return


def Read_ADC_CH(ch):
    hex_use = bytearray()
    hex_use.append(8)
    hex_use.append(ch)
    hex_use.append(0)
    hex_use.append(0)
    hex_use.append(0)
    hex_use.append(0)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return recv[4] * 256 + recv[5]


# ---------------------------------------------------------------------------
# MSN 设备数据结构 (SFR 表解析)
# ---------------------------------------------------------------------------
def Read_M_SFR_Data(add):
    """从 add 起读取 256 字节, 解析出所有 MSN 数据项存入 My_MSN_Data。"""
    SFR_data = bytearray()
    for i in range(0, 256):
        SFR_data.append(Read_M_u8(add + i))
    data_type = 0
    data_num = 0
    data_len = 0
    data_use = bytearray()
    data_name = b''
    data_unit = b''
    data_family = b''
    data_data = b''
    for i in range(0, 256):
        if SFR_data[i] != 0 and data_type < 3:
            data_use.append(SFR_data[i])
        elif data_type < 3:
            if len(data_use) == 0:
                return
            if data_type == 0:
                data_name = data_use
                data_type = 1
            elif data_type == 1:
                data_unit = data_use
                data_type = 2
            elif data_type == 2:
                data_family = data_use
                data_type = 3
                if int(ord(data_use) // 32) == 0:
                    data_len = 2
                elif int(ord(data_use) // 32) == 1:
                    data_len = 1
                elif int(ord(data_use) // 32) == 2:
                    data_len = 2
                elif int(ord(data_use) // 32) == 3:
                    data_len = data_family[0] % 32
            data_use = bytearray()
            continue
        if data_len > 0 and data_type == 3:
            data_use.append(SFR_data[i])
            data_len = data_len - 1
        if data_len == 0 and data_type == 3:
            data_data = data_use
            data_type = 0
            My_MSN_Data.append(MSN_Data(data_name, data_unit, data_family, data_data))
            data_use = bytearray()


def Print_MSN_Data():
    num = len(My_MSN_Data)
    data_str = ''
    print('MSN数据总数为：' + str(num))
    for i in range(0, num):
        data_str = data_str + '序号：' + str(i) + '    名称：' + str(My_MSN_Data[i].name) + '    单位:' + str(My_MSN_Data[i].unit)
        if ord(My_MSN_Data[i].family) // 32 == 0:
            data_str = data_str + '    类型：u8_SFR地址,长度' + str(ord(My_MSN_Data[i].family) % 32)
            data_str = data_str + '    地址：' + str(int(My_MSN_Data[i].data[0]) * 256 + int(My_MSN_Data[i].data[1]))
        elif ord(My_MSN_Data[i].family) // 32 == 1:
            data_str = data_str + '    类型：u16_SFR地址,长度' + str(ord(My_MSN_Data[i].family) % 32)
            data_str = data_str + '    地址：' + str(int(My_MSN_Data[i].data[0]))
        elif ord(My_MSN_Data[i].family) // 32 == 2:
            data_str = data_str + '    类型：u32_SFR地址,长度：' + str(ord(My_MSN_Data[i].family) % 32)
            data_str = data_str + '    地址：' + str(int(My_MSN_Data[i].data[0]) * 256 + int(My_MSN_Data[i].data[1]))
        elif ord(My_MSN_Data[i].family) // 32 == 3:
            data_str = data_str + '    类型：字符串,长度' + str(ord(My_MSN_Data[i].family) % 32)
            data_str = data_str + '    数据：' + str(My_MSN_Data[i].data)
        elif ord(My_MSN_Data[i].family) // 32 == 4:
            data_str = data_str + '    类型：u8数组数据,长度' + str(int(My_MSN_Data[i].family) % 32)
            data_str = data_str + '    数据：' + str(My_MSN_Data[i].data)
        print(data_str)
        data_str = ''


def Read_MSN_Data(name_use):
    num = len(My_MSN_Data)
    use_data = []
    for i in range(0, num):
        if My_MSN_Data[i].name == name_use:
            if ord(My_MSN_Data[i].family) // 32 == 0:
                sfr_add = int(My_MSN_Data[i].data[0]) * 256 + int(My_MSN_Data[i].data[1])
                for n in range(0, ord(My_MSN_Data[i].family) % 32):
                    use_data.append(Read_M_u8(sfr_add + n))
            elif ord(My_MSN_Data[i].family) // 32 == 1:
                use_data = Read_M_u16(int(My_MSN_Data[i].data[0]))
            elif ord(My_MSN_Data[i].family) // 32 == 3:
                use_data = My_MSN_Data[i].data
            elif ord(My_MSN_Data[i].family) // 32 == 4:
                use_data = My_MSN_Data[i].data
            print(str(My_MSN_Data[i].name) + '=' + str(use_data))
            return use_data
    if name_use != 0:
        print('"' + name_use + '"' + '不存在,请检查名称是否正确')
    return 0


def Write_MSN_Data(name_use, data_w):
    num = len(My_MSN_Data)
    for i in range(0, num):
        if My_MSN_Data[i].name == name_use:
            if int(My_MSN_Data[i].family) // 32 == 0:
                Write_M_u8(int(My_MSN_Data[i].data[0]) * 256 + int(My_MSN_Data[i].data[1]), data_w)
                print('"' + name_use + '"' + '写入' + str(data_w) + '完成')
                return 0
            if int(My_MSN_Data[i].family) // 32 == 1:
                Write_M_u16(int(My_MSN_Data[i].data[0]), data_w)
                print('"' + name_use + '"' + '写入' + str(data_w) + '完成')
                return 0
    print('"' + name_use + '"' + '不存在,请检查名称是否正确')


# ---------------------------------------------------------------------------
# Flash 操作
# ---------------------------------------------------------------------------
def Write_Flash_Page(Page_add, data_w, Page_num):
    """向 Flash 写一页 (256 字节)。"""
    hex_use = bytearray()
    for i in range(0, 64):
        hex_use.append(4)
        hex_use.append(i)
        hex_use.append(data_w[i * 4 + 0])
        hex_use.append(data_w[i * 4 + 1])
        hex_use.append(data_w[i * 4 + 2])
        hex_use.append(data_w[i * 4 + 3])
        SER_Write(hex_use)
    hex_use = bytearray()
    hex_use.append(3)
    hex_use.append(1)
    hex_use.append(Page_add // 65536)
    hex_use.append((Page_add % 65536) // 256)
    hex_use.append(Page_add % 65536 % 256)
    hex_use.append(Page_num % 256)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return


def Write_Flash_Page_fast(Page_add, data_w, Page_num):
    """向 Flash 写一页 (256 字节) - 拼接成一条数据包发送。"""
    hex_use = b''
    for i in range(0, 64):
        hex_use = hex_use + int(4).to_bytes(1, 'little')
        hex_use = hex_use + int(i).to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 0].to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 1].to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 2].to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 3].to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_add // 65536).to_bytes(1, 'little')
    hex_use = hex_use + int((Page_add % 65536) // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_add % 65536 % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_num).to_bytes(1, 'little')
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return


def Erase_Flash_page(add, size):
    hex_use = bytearray()
    hex_use.append(3)
    hex_use.append(2)
    hex_use.append((add % 65536) // 256)
    hex_use.append(add % 65536 % 256)
    hex_use.append((size % 65536) // 256)
    hex_use.append(size % 65536 % 256)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            return


def Read_Flash_byte(add):
    hex_use = bytearray()
    hex_use.append(3)
    hex_use.append(0)
    hex_use.append(add // 65536)
    hex_use.append((add % 65536) // 256)
    hex_use.append(add % 65536 % 256)
    hex_use.append(0)
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            print(recv[5])
            return recv[5]


def Write_Flash_Photo_fast(Page_add, Photo_name):
    """将 .bin 文件烧写进 Flash。"""
    filepath = Photo_name + '.bin'
    try:
        binfile = open(filepath, 'rb')
    except:
        print('找不到“' + filepath + '”文件,请检查其位置是否位于当前目录下')
        GUI_Text_Append('文件路径或格式出错!\n')
        return 0
    Fsize = os.path.getsize(filepath)
    print('找到“' + filepath + '”文件,大小：' + str(Fsize) + ' B')
    GUI_Text_Append('大小' + str(Fsize) + 'B,烧录中...\n')
    u_time = time.time()
    if Fsize % 256 != 0:
        Erase_Flash_page(Page_add, Fsize // 256 + 1)
    else:
        Erase_Flash_page(Page_add, Fsize // 256)
    for i in range(0, Fsize // 256):
        Fdata = binfile.read(256)
        Write_Flash_Page_fast(Page_add + i, Fdata, 1)
    if Fsize % 256 != 0:
        Fdata = binfile.read(Fsize % 256)
        for i in range(Fsize % 256, 256):
            Fdata = Fdata + int(255).to_bytes(1, 'little')
        Write_Flash_Page_fast(Page_add + Fsize // 256, Fdata, 1)
    u_time = time.time() - u_time
    print(filepath + ' 烧写完成,耗时' + str(u_time) + '秒')
    GUI_Text_Append('烧写完成,耗时' + str(int(u_time * 1000)) + 'ms\n')


def Write_Flash_hex_fast(Page_add, img_use):
    """将 RGB565 数据烧写进 Flash。"""
    Fsize = len(img_use)
    GUI_Text_Append('大小' + str(Fsize) + 'B,烧录中...\n')
    u_time = time.time()
    if Fsize % 256 != 0:
        Erase_Flash_page(Page_add, Fsize // 256 + 1)
    else:
        Erase_Flash_page(Page_add, Fsize // 256)
    for i in range(0, Fsize // 256):
        Fdata = img_use[:256]
        img_use = img_use[256:]
        Write_Flash_Page_fast(Page_add + i, Fdata, 1)
    if Fsize % 256 != 0:
        Fdata = img_use
        for i in range(Fsize % 256, 256):
            Fdata = Fdata + int(255).to_bytes(1, 'little')
        Write_Flash_Page_fast(Page_add + Fsize // 256, Fdata, 1)
    u_time = time.time() - u_time
    GUI_Text_Append('烧写完成,耗时' + str(int(u_time * 1000)) + 'ms\n')


def Write_Flash_ZK(Page_add, ZK_name):
    """烧写字库 (.bin, 去掉 6 字节头)。"""
    filepath = ZK_name + '.bin'
    try:
        binfile = open(filepath, 'rb')
    except:
        print('找不到“' + filepath + '”文件,请检查其位置是否位于当前目录下')
        return 0
    Fsize = os.path.getsize(filepath) - 6
    print('找到“' + filepath + '”文件,大小：' + str(Fsize) + ' B')
    for i in range(0, Fsize // 256):
        Fdata = binfile.read(256)
        Write_Flash_Page(Page_add + i, Fdata, 1)
    if Fsize % 256 != 0:
        Fdata = binfile.read(Fsize % 256)
        for i in range(Fsize % 256, 256):
            Fdata = Fdata + int(255).to_bytes(1, 'little')
        Write_Flash_Page(Page_add + Fsize // 256, Fdata, 1)
    print(filepath + ' 烧写完成')


# ---------------------------------------------------------------------------
# LCD 显示命令
# ---------------------------------------------------------------------------
def LCD_Set_XY(LCD_D0, LCD_D1):
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D0 // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D0 % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D1 // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D1 % 256).to_bytes(1, 'little')
    SER_Write(hex_use)


def LCD_Set_Size(LCD_D0, LCD_D1):
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(1).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D0 // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D0 % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D1 // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D1 % 256).to_bytes(1, 'little')
    SER_Write(hex_use)


def LCD_Set_Color(LCD_D0, LCD_D1):
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D0 // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D0 % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D1 // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_D1 % 256).to_bytes(1, 'little')
    SER_Write(hex_use)


def LCD_Photo(LCD_X, LCD_Y, LCD_X_Size, LCD_Y_Size, Page_Add):
    """直接显示 Flash 中某页图片。"""
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Size(LCD_X_Size, LCD_Y_Size)
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_Add // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_Add % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_ADD(LCD_X, LCD_Y, LCD_X_Size, LCD_Y_Size):
    """设置显示区域 (用于截图/数据流显示)。"""
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Size(LCD_X_Size, LCD_Y_Size)
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(7).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_State(LCD_S):
    global Device_State
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(10).to_bytes(1, 'little')
    hex_use = hex_use + int(LCD_S).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_DATA(data_w, size):
    """发送 64 像素 (256 字节) 显示数据。"""
    hex_use = b''
    for i in range(0, 64):
        hex_use = hex_use + int(4).to_bytes(1, 'little')
        hex_use = hex_use + int(i).to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 0].to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 1].to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 2].to_bytes(1, 'little')
        hex_use = hex_use + data_w[i * 4 + 3].to_bytes(1, 'little')
    hex_use = hex_use + int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(8).to_bytes(1, 'little')
    hex_use = hex_use + int(size // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(size % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)


def Write_LCD_Photo_fast(x_star, y_star, x_size, y_size, Photo_name):
    """读取 .bin 图片数据流式显示到 LCD。"""
    filepath = Photo_name + '.bin'
    try:
        binfile = open(filepath, 'rb')
    except:
        print('找不到“' + filepath + '”文件,请检查其位置是否位于当前目录下')
        return 0
    Fsize = os.path.getsize(filepath)
    print('找到“' + filepath + '”文件,大小：' + str(Fsize) + ' B')
    u_time = time.time()
    LCD_ADD(x_star, y_star, x_size, y_size)
    for i in range(0, Fsize // 256):
        Fdata = binfile.read(256)
        LCD_DATA(Fdata, 256)
    if Fsize % 256 != 0:
        Fdata = binfile.read(Fsize % 256)
        for i in range(Fsize % 256, 256):
            Fdata = Fdata + int(255).to_bytes(1, 'little')
        LCD_DATA(Fdata, Fsize % 256)
    u_time = time.time() - u_time
    print(filepath + ' 显示完成,耗时' + str(u_time) + '秒')


def Write_LCD_Photo_fast1(x_star, y_star, x_size, y_size, Photo_name):
    """读取 .bin 图片数据, 一次性拼接后发送显示。"""
    filepath = Photo_name + '.bin'
    try:
        binfile = open(filepath, 'rb')
    except:
        print('找不到“' + filepath + '”文件,请检查其位置是否位于当前目录下')
        return 0
    Fsize = os.path.getsize(filepath)
    print('找到“' + filepath + '”文件,大小：' + str(Fsize) + ' B')
    u_time = time.time()
    LCD_ADD(x_star, y_star, x_size, y_size)
    hex_use = bytearray()
    for j in range(0, Fsize // 256):
        data_w = binfile.read(256)
        for i in range(0, 64):
            hex_use.append(4)
            hex_use.append(i)
            hex_use.append(data_w[i * 4 + 0])
            hex_use.append(data_w[i * 4 + 1])
            hex_use.append(data_w[i * 4 + 2])
            hex_use.append(data_w[i * 4 + 3])
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(1)
        hex_use.append(0)
        hex_use.append(0)
    if Fsize % 256 != 0:
        data_w = binfile.read(Fsize % 256)
        for i in range(Fsize % 256, 256):
            data_w = data_w + int(255).to_bytes(1, 'little')
        for i in range(0, 64):
            hex_use.append(4)
            hex_use.append(i)
            hex_use.append(data_w[i * 4 + 0])
            hex_use.append(data_w[i * 4 + 1])
            hex_use.append(data_w[i * 4 + 2])
            hex_use.append(data_w[i * 4 + 3])
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(0)
        hex_use.append(Fsize % 256)
        hex_use.append(0)
    hex_use.append(2)
    hex_use.append(3)
    hex_use.append(9)
    hex_use.append(0)
    hex_use.append(0)
    hex_use.append(0)
    SER_Write(hex_use)
    u_time = time.time() - u_time
    print(filepath + ' 显示完成,耗时' + str(u_time) + '秒')


def Write_LCD_Screen_fast(x_star, y_star, x_size, y_size, Photo_data):
    """将 RGB565 屏幕数据流式发送显示 (含主色压缩)。"""
    LCD_ADD(x_star, y_star, x_size, y_size)
    Photo_data_use = Photo_data
    hex_use = bytearray()
    for j in range(0, x_size * y_size * 2 // 256):
        data_w = Photo_data_use[:256]
        Photo_data_use = Photo_data_use[256:]
        cmp_use = []
        for i in range(0, 64):
            cmp_use.append(data_w[i * 4 + 0] * 256 * 256 * 256 + data_w[i * 4 + 1] * 256 * 256 + data_w[i * 4 + 2] * 256 + data_w[i * 4 + 3])
        result = max(set(cmp_use), key=cmp_use.count)
        hex_use.append(2)
        hex_use.append(4)
        color_ram = result
        hex_use.append(color_ram // 16777216)
        color_ram = color_ram % 16777216
        hex_use.append(color_ram // 65536)
        color_ram = color_ram % 65536
        hex_use.append(color_ram // 256)
        hex_use.append(color_ram % 256)
        for i in range(0, 64):
            if data_w[i * 4 + 0] * 256 * 256 * 256 + data_w[i * 4 + 1] * 256 * 256 + data_w[i * 4 + 2] * 256 + data_w[i * 4 + 3] != result:
                hex_use.append(4)
                hex_use.append(i)
                hex_use.append(data_w[i * 4 + 0])
                hex_use.append(data_w[i * 4 + 1])
                hex_use.append(data_w[i * 4 + 2])
                hex_use.append(data_w[i * 4 + 3])
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(1)
        hex_use.append(0)
        hex_use.append(0)
    if x_size * y_size * 2 % 256 != 0:
        data_w = Photo_data_use
        for i in range(x_size * y_size * 2 % 256, 256):
            data_w.append(255)
        for i in range(0, 64):
            hex_use.append(4)
            hex_use.append(i)
            hex_use.append(data_w[i * 4 + 0])
            hex_use.append(data_w[i * 4 + 1])
            hex_use.append(data_w[i * 4 + 2])
            hex_use.append(data_w[i * 4 + 3])
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(0)
        hex_use.append(x_size * y_size * 2 % 256)
        hex_use.append(0)
    SER_Write(hex_use)


def Write_LCD_Screen_fast1(x_star, y_star, x_size, y_size, Photo_data):
    """将 RGB565 屏幕数据一次性拼接后发送显示。"""
    LCD_ADD(x_star, y_star, x_size, y_size)
    Photo_data_use = Photo_data
    hex_use = bytearray()
    for j in range(0, x_size * y_size * 2 // 256):
        data_w = Photo_data_use[:256]
        Photo_data_use = Photo_data_use[256:]
        for i in range(0, 64):
            hex_use.append(4)
            hex_use.append(i)
            hex_use.append(data_w[i * 4 + 0])
            hex_use.append(data_w[i * 4 + 1])
            hex_use.append(data_w[i * 4 + 2])
            hex_use.append(data_w[i * 4 + 3])
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(1)
        hex_use.append(0)
        hex_use.append(0)
    if x_size * y_size * 2 % 256 != 0:
        data_w = Photo_data_use
        for i in range(x_size * y_size * 2 % 256, 256):
            data_w.append(255)
        for i in range(0, 64):
            hex_use.append(4)
            hex_use.append(i)
            hex_use.append(data_w[i * 4 + 0])
            hex_use.append(data_w[i * 4 + 1])
            hex_use.append(data_w[i * 4 + 2])
            hex_use.append(data_w[i * 4 + 3])
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(0)
        hex_use.append(x_size * y_size * 2 % 256)
        hex_use.append(0)
    hex_use.append(2)
    hex_use.append(3)
    hex_use.append(9)
    hex_use.append(0)
    hex_use.append(0)
    hex_use.append(0)
    SER_Write(hex_use)


def LCD_Photo_wb(LCD_X, LCD_Y, LCD_X_Size, LCD_Y_Size, Page_Add, LCD_FC, LCD_BC):
    """按前景/背景色显示 Flash 中黑白图片。"""
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Size(LCD_X_Size, LCD_Y_Size)
    LCD_Set_Color(LCD_FC, LCD_BC)
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(1).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_Add // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_Add % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_ASCII_32X64(LCD_X, LCD_Y, Txt, LCD_FC, LCD_BC, Num_Page):
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Color(LCD_FC, LCD_BC)
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(ord(Txt)).to_bytes(1, 'little')
    hex_use = hex_use + int(Num_Page // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(Num_Page % 256).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_GB2312_16X16(LCD_X, LCD_Y, Txt, LCD_FC, LCD_BC):
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Color(LCD_FC, LCD_BC)
    Txt_Data = Txt.encode('gb2312')
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(Txt_Data[0]).to_bytes(1, 'little')
    hex_use = hex_use + int(Txt_Data[1]).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_Photo_wb_MIX(LCD_X, LCD_Y, LCD_X_Size, LCD_Y_Size, Page_Add, LCD_FC, BG_Page):
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Size(LCD_X_Size, LCD_Y_Size)
    LCD_Set_Color(LCD_FC, BG_Page)
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(4).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_Add // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(Page_Add % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_ASCII_32X64_MIX(LCD_X, LCD_Y, Txt, LCD_FC, BG_Page, Num_Page):
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Color(LCD_FC, BG_Page)
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(5).to_bytes(1, 'little')
    hex_use = hex_use + int(ord(Txt)).to_bytes(1, 'little')
    hex_use = hex_use + int(Num_Page // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(Num_Page % 256).to_bytes(1, 'little')
    SER_Write(hex_use)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_GB2312_16X16_MIX(LCD_X, LCD_Y, Txt, LCD_FC, BG_Page):
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Color(LCD_FC, BG_Page)
    Txt_Data = Txt.encode('gb2312')
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(6).to_bytes(1, 'little')
    hex_use = hex_use + int(Txt_Data[0]).to_bytes(1, 'little')
    hex_use = hex_use + int(Txt_Data[1]).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.2)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


def LCD_Color_set(LCD_X, LCD_Y, LCD_X_Size, LCD_Y_Size, F_Color):
    global Device_State
    LCD_Set_XY(LCD_X, LCD_Y)
    LCD_Set_Size(LCD_X_Size, LCD_Y_Size)
    hex_use = int(2).to_bytes(1, 'little')
    hex_use = hex_use + int(3).to_bytes(1, 'little')
    hex_use = hex_use + int(11).to_bytes(1, 'little')
    hex_use = hex_use + int(F_Color // 256).to_bytes(1, 'little')
    hex_use = hex_use + int(F_Color % 256).to_bytes(1, 'little')
    hex_use = hex_use + int(0).to_bytes(1, 'little')
    SER_Write(hex_use)
    time.sleep(0.001)
    while True:
        recv = SER_Read()
        if recv == 0:
            return 0
        if len(recv) != 0:
            if recv[0] != hex_use[0] or recv[1] != hex_use[1]:
                Device_State = 0
            return


# ---------------------------------------------------------------------------
# 显示页面 (状态机各页)
# ---------------------------------------------------------------------------
def TIM1():
    """0.2s 定时器回调, 置 time_out=1 供按键检测使用。"""
    global time_out, timer1
    time_out = 1
    timer1 = threading.Timer(0.2, TIM1)
    timer1.start()


def show_gif():
    """页面 0: 循环播放动图 (36 帧)。"""
    global State_change, gif_num
    if State_change == 1:
        State_change = 0
        gif_num = 0
    if State_change == 0:
        LCD_Photo(0, 0, 160, 80, gif_num * 100)
        gif_num = gif_num + 1
        if gif_num > 35:
            gif_num = 0
    time.sleep(0.05)


def show_PC_state(FC, BC):
    """页面 1/2: 显示 CPU / 内存 / 电池 / 磁盘占用。"""
    global State_change
    photo_add = 4038
    num_add = 4026
    if State_change == 1:
        State_change = 0
        LCD_Photo_wb(0, 0, 160, 80, photo_add, FC, BC)
    if State_change == 0:
        CPU = int(psutil.cpu_percent(interval=0.5))
        mem = psutil.virtual_memory()
        RAM = int(mem.percent)
        battery = psutil.sensors_battery()
        if battery != None:
            BAT = int(battery.percent)
        else:
            BAT = 100
        FRQ = int(psutil.disk_usage('/').used * 100 / psutil.disk_usage('/').total)
        if CPU >= 100:
            LCD_Photo_wb(24, 0, 8, 33, 10 + num_add, FC, BC)
            CPU = CPU % 100
        else:
            LCD_Photo_wb(24, 0, 8, 33, 11 + num_add, FC, BC)
        LCD_Photo_wb(32, 0, 24, 33, CPU // 10 + num_add, FC, BC)
        LCD_Photo_wb(56, 0, 24, 33, CPU % 10 + num_add, FC, BC)
        if RAM >= 100:
            LCD_Photo_wb(104, 0, 8, 33, 10 + num_add, FC, BC)
            RAM = RAM % 100
        else:
            LCD_Photo_wb(104, 0, 8, 33, 11 + num_add, FC, BC)
        LCD_Photo_wb(112, 0, 24, 33, RAM // 10 + num_add, FC, BC)
        LCD_Photo_wb(136, 0, 24, 33, RAM % 10 + num_add, FC, BC)
        if BAT >= 100:
            LCD_Photo_wb(104, 47, 8, 33, 10 + num_add, FC, BC)
            BAT = BAT % 100
        else:
            LCD_Photo_wb(104, 47, 8, 33, 11 + num_add, FC, BC)
        LCD_Photo_wb(112, 47, 24, 33, BAT // 10 + num_add, FC, BC)
        LCD_Photo_wb(136, 47, 24, 33, BAT % 10 + num_add, FC, BC)
        if FRQ >= 100:
            LCD_Photo_wb(24, 47, 8, 33, 10 + num_add, FC, BC)
            FRQ = FRQ % 100
        else:
            LCD_Photo_wb(24, 47, 8, 33, 11 + num_add, FC, BC)
        LCD_Photo_wb(32, 47, 24, 33, FRQ // 10 + num_add, FC, BC)
        LCD_Photo_wb(56, 47, 24, 33, FRQ % 10 + num_add, FC, BC)


def show_Photo1():
    """页面 3: 显示相册图片 (Flash 地址 3926)。"""
    global State_change
    FC = BLUE
    BC = BLACK
    if State_change == 1:
        State_change = 0
        LCD_Photo(0, 0, 160, 80, 3926)
    if State_change == 0:
        time.sleep(0.2)


def show_PC_time():
    """页面 4: 显示 PC 时间 (时:分, 32X64 大数字)。"""
    global State_change
    FC = YELLOW
    photo_add = 3826
    num_add = 3651
    if State_change == 1:
        State_change = 0
        LCD_Photo(0, 0, 160, 80, photo_add)
        LCD_ASCII_32X64_MIX(64, 8, ':', FC, photo_add, num_add)
    if State_change == 0:
        time_h = int(datetime.now().hour)
        time_m = int(datetime.now().minute)
        time_S = int(datetime.now().second)
        LCD_ASCII_32X64_MIX(8, 8, chr(time_h // 10 + 48), FC, photo_add, num_add)
        LCD_ASCII_32X64_MIX(40, 8, chr(time_h % 10 + 48), FC, photo_add, num_add)
        LCD_ASCII_32X64_MIX(88, 8, chr(time_m // 10 + 48), FC, photo_add, num_add)
        LCD_ASCII_32X64_MIX(120, 8, chr(time_m % 10 + 48), FC, photo_add, num_add)
        time.sleep(0.2)


# ---------------------------------------------------------------------------
# 屏幕截图显示 (页面 5)
# ---------------------------------------------------------------------------
def Screen_Date_Process(Photo_data):
    """将 RGB565 像素列表压缩为 LCD 显示数据 (取主色 + 差异像素)。"""
    global size_USE_X1, size_USE_Y1
    Photo_data_use = Photo_data
    hex_use = bytearray()
    for j in range(0, size_USE_X1 * size_USE_Y1 // 128):
        data_w = Photo_data_use[:128]
        Photo_data_use = Photo_data_use[128:]
        cmp_use = []
        for i in range(0, 64):
            cmp_use.append(data_w[i * 2 + 0] * 65536 + data_w[i * 2 + 1])
        result = max(set(cmp_use), key=cmp_use.count)
        hex_use.append(2)
        hex_use.append(4)
        color_ram = result
        hex_use.append(color_ram >> 24)
        color_ram = color_ram % 16777216
        hex_use.append(color_ram >> 16)
        color_ram = color_ram % 65536
        hex_use.append(color_ram >> 8)
        hex_use.append(color_ram % 256)
        for i in range(0, 64):
            if data_w[i * 2 + 0] * 65536 + data_w[i * 2 + 1] != result:
                hex_use.append(4)
                hex_use.append(i)
                hex_use.append(data_w[i * 2 + 0] >> 8)
                hex_use.append(data_w[i * 2 + 0] % 256)
                hex_use.append(data_w[i * 2 + 1] >> 8)
                hex_use.append(data_w[i * 2 + 1] % 256)
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(1)
        hex_use.append(0)
        hex_use.append(0)
    if size_USE_X1 * size_USE_Y1 * 2 % 256 != 0:
        data_w = Photo_data_use
        for i in range(size_USE_X1 * size_USE_Y1 * 2 % 256, 256):
            data_w.append(65535)
        for i in range(0, 64):
            hex_use.append(4)
            hex_use.append(i)
            hex_use.append(data_w[i * 2 + 0] >> 8)
            hex_use.append(data_w[i * 2 + 0] % 256)
            hex_use.append(data_w[i * 2 + 1] >> 8)
            hex_use.append(data_w[i * 2 + 1] % 256)
        hex_use.append(2)
        hex_use.append(3)
        hex_use.append(8)
        hex_use.append(0)
        hex_use.append(size_USE_X1 * size_USE_Y1 * 2 % 256)
        hex_use.append(0)
    return hex_use


def Stop_Thread1():
    """请求截图线程退出 (threading.Thread 无法直接 stop, 用标志位实现)。"""
    global thread1_stop
    thread1_stop = 1


def Start_Thread1():
    """重启截图线程: 先等待旧线程退出, 再新建线程。"""
    global Thread1, thread1_stop
    thread1_stop = 0
    try:
        if Thread1 is not None and Thread1.is_alive():
            Thread1.join(timeout=1)
    except:
        pass
    Thread1 = threading.Thread(target=Screen_Date_get, daemon=True)
    Thread1.start()


def Screen_Date_get():
    """截图线程: 周期性抓取屏幕并生成 RGB565 数据。"""
    global size_USE_X1, size_USE_Y1, G_screnn0, G_screnn1, G_screnn0_OK, G_screnn1_OK
    print('截图线程创建成功')
    size_PC = pyautogui.size()
    size_mode = 0
    if size_mode == 0:
        if size_PC.width >= size_PC.height * 2:
            size_USE_X1 = 160
            size_USE_Y1 = 160 * size_PC.height // size_PC.width
        else:
            size_USE_X1 = 160
            size_USE_Y1 = 80
    elif size_mode == 1:
        if size_PC.height * 2 >= size_PC.width:
            size_USE_X1 = 80 * size_PC.width // size_PC.height
            size_USE_Y1 = 80
        else:
            size_USE_X1 = 160
            size_USE_Y1 = 80
    elif size_mode == 2:
        size_USE_X1 = 160
        size_USE_Y1 = 80
    while True:
        if thread1_stop == 1:
            print('截图线程已退出')
            return
        if G_screnn0_OK == 0 or G_screnn1_OK == 0:
            u_time1 = time.time()
            hex_16RGB = []
            im = pyautogui.screenshot()
            if size_mode == 0:
                if size_PC.width >= size_PC.height * 2:
                    im1 = im.resize((size_USE_X1, size_USE_Y1))
                else:
                    im1 = im.resize((160, 160 * size_PC.height // size_PC.width))
                    im1 = im1.crop((0, (160 * size_PC.height // size_PC.width - 80) // 2, 160, (160 * size_PC.height // size_PC.width - 80) // 2 + 80))
            elif size_mode == 1:
                if size_PC.height * 2 >= size_PC.width:
                    im1 = im.resize((size_USE_X1, size_USE_Y1))
                else:
                    im1 = im.resize((80 * size_PC.width // size_PC.height, 80))
                    im1 = im1.crop(((80 * size_PC.width // size_PC.height - 160) // 2, 0, (80 * size_PC.width // size_PC.height - 160) // 2 + 160, 80))
            elif size_mode == 2:
                im1 = im.resize((size_USE_X1, size_USE_Y1))
            im2 = im1.load()
            for y in range(0, size_USE_Y1):
                for x in range(0, size_USE_X1):
                    hex_16RGB.append((im2[(x, y)][0] >> 3) << 11 | (im2[(x, y)][1] >> 2) << 5 | im2[(x, y)][2] >> 3)
            if G_screnn0_OK == 0:
                G_screnn0 = Screen_Date_Process(hex_16RGB)
                G_screnn0_OK = 1
            elif G_screnn1_OK == 0:
                G_screnn1 = Screen_Date_Process(hex_16RGB)
                G_screnn1_OK = 1
            u_time1 = time.time() - u_time1
            #print('截屏耗时' + str(u_time1))
        time.sleep(0.001)


def show_PC_Screen():
    """页面 5: 将截图数据发送到 LCD 显示。"""
    global State_change, Screen_Error, G_screnn0, G_screnn1, G_screnn0_OK, G_screnn1_OK
    if State_change == 1:
        State_change = 0
        Screen_Error = 0
        LCD_ADD((160 - size_USE_X1) // 2, (80 - size_USE_Y1) // 2, size_USE_X1, size_USE_Y1)
    if State_change == 0:
        if G_screnn0_OK == 1 or G_screnn1_OK == 1:
            u_time = time.time()
            if G_screnn0_OK == 1:
                SER_Write(G_screnn0)
                G_screnn0_OK = 0
            elif G_screnn1_OK == 1:
                SER_Write(G_screnn1)
                G_screnn1_OK = 0
            u_time = time.time() - u_time
            Screen_Error = 0
        else:
            Screen_Error = Screen_Error + 1
            if Screen_Error > 1000:
                Screen_Error = 0
                print('截图线程可能卡死,正在重启...')
                Stop_Thread1()
                Start_Thread1()
            time.sleep(0.001)


# ---------------------------------------------------------------------------
# GUI (PyQt6)
# ---------------------------------------------------------------------------
def UI_Page():
    global bridge, Label1, Label2, Label3, Label4, Label5, Label6, Text1
    global s1, s2, s3, root
    root = QWidget()
    root.setWindowTitle('USB屏幕助手V1.0')
    size_show = pyautogui.size()
    Show_X = int(size_show.width / 2) - int(Show_W / 2)
    Show_Y = int(size_show.height / 2) - int(Show_H / 2)
    root.setFixedSize(Show_W, Show_H)
    root.move(Show_X, Show_Y)

    btn1 = QPushButton('上翻页', root)
    btn1.setFixedSize(64, 28)
    btn1.move(400, 261)
    btn1.clicked.connect(Page_UP)

    btn2 = QPushButton('下翻页', root)
    btn2.setFixedSize(64, 28)
    btn2.move(400, 311)
    btn2.clicked.connect(Page_Down)

    btn3 = QPushButton('选择背景图像', root)
    btn3.setFixedSize(96, 28)
    btn3.move(250, 61)
    btn3.clicked.connect(Get_Photo_Path1)

    btn4 = QPushButton('选择闪存固件', root)
    btn4.setFixedSize(96, 28)
    btn4.move(250, 111)
    btn4.clicked.connect(Get_Photo_Path2)

    btn5 = QPushButton('烧写', root)
    btn5.setFixedSize(64, 28)
    btn5.move(400, 61)
    btn5.clicked.connect(Writet_Photo_Path1)

    btn6 = QPushButton('烧写', root)
    btn6.setFixedSize(64, 28)
    btn6.move(400, 111)
    btn6.clicked.connect(Writet_Photo_Path2)

    btn7 = QPushButton('切换显示方向', root)
    btn7.setFixedSize(96, 28)
    btn7.move(250, 311)
    btn7.clicked.connect(LCD_Change)

    btn8 = QPushButton('烧写', root)
    btn8.setFixedSize(64, 28)
    btn8.move(400, 161)
    btn8.clicked.connect(Writet_Photo_Path3)

    btn9 = QPushButton('烧写', root)
    btn9.setFixedSize(64, 28)
    btn9.move(400, 211)
    btn9.clicked.connect(Writet_Photo_Path4)

    btn10 = QPushButton('选择相册图像', root)
    btn10.setFixedSize(96, 28)
    btn10.move(250, 161)
    btn10.clicked.connect(Get_Photo_Path3)

    btn11 = QPushButton('选择动图文件', root)
    btn11.setFixedSize(96, 28)
    btn11.move(250, 211)
    btn11.clicked.connect(Get_Photo_Path4)

    s1 = QSlider(Qt.Orientation.Horizontal, root)
    s1.setRange(0, 31)
    s1.setValue(31)
    s1.setFixedSize(80, 24)
    s1.move(150, 13)
    s1.setStyleSheet('QSlider::groove:horizontal { background: red; height: 6px; }')
    s1.valueChanged.connect(_set_S1)

    s2 = QSlider(Qt.Orientation.Horizontal, root)
    s2.setRange(0, 63)
    s2.setValue(0)
    s2.setFixedSize(80, 24)
    s2.move(250, 13)
    s2.setStyleSheet('QSlider::groove:horizontal { background: green; height: 6px; }')
    s2.valueChanged.connect(_set_S2)

    s3 = QSlider(Qt.Orientation.Horizontal, root)
    s3.setRange(0, 31)
    s3.setValue(0)
    s3.setFixedSize(80, 24)
    s3.move(350, 13)
    s3.setStyleSheet('QSlider::groove:horizontal { background: blue; height: 6px; }')
    s3.valueChanged.connect(_set_S3)

    Label1 = QLabel('设备未连接', root)
    Label1.setFixedHeight(24)
    Label1.setStyleSheet('background-color: red;')
    Label1.move(25, 13)

    Label2 = QLabel('', root)
    Label2.setFixedSize(24, 24)
    Label2.setStyleSheet('background-color: red;')
    Label2.move(460, 13)

    Label3 = QLabel('', root)
    Label3.setFixedSize(170, 24)
    Label3.setStyleSheet('background-color: white;')
    Label3.move(5, 63)

    Label4 = QLabel('', root)
    Label4.setFixedSize(170, 24)
    Label4.setStyleSheet('background-color: white;')
    Label4.move(5, 113)

    Label5 = QLabel('', root)
    Label5.setFixedSize(170, 24)
    Label5.setStyleSheet('background-color: white;')
    Label5.move(5, 163)

    Label6 = QLabel('', root)
    Label6.setFixedSize(170, 24)
    Label6.setStyleSheet('background-color: white;')
    Label6.move(5, 213)

    Text1 = QPlainTextEdit(root)
    Text1.setFixedSize(170, 84)
    Text1.move(5, 258)
    Text1.setReadOnly(True)
    Text1.clear()

    bridge = GUI_Bridge()
    bridge.label1_text.connect(Label1.setText)
    bridge.label1_bg.connect(_label1_set_bg)
    bridge.label2_bg.connect(_label2_set_bg)
    bridge.label3_text.connect(Label3.setText)
    bridge.label4_text.connect(Label4.setText)
    bridge.label5_text.connect(Label5.setText)
    bridge.label6_text.connect(Label6.setText)
    bridge.text1_clear.connect(_text1_clear)
    bridge.text1_append.connect(_text1_append)

    root.show()


# ---------------------------------------------------------------------------
# 设备枚举 / 连接
# ---------------------------------------------------------------------------
def Get_MSN_Device():
    """扫描串口, 识别 MSN 设备, 读取其数据表并启动截图线程。"""
    global Thread1, My_MSN_Device, My_MSN_Data, Device_State, State_change, Screen_Error, ADC_det, LCD_Change_now, ser
    # 断开旧连接: 先停截图线程并释放串口, 否则端口被占用导致无法重连
    Stop_Thread1()
    try:
        if ser is not None and ser.is_open:
            ser.close()
    except:
        pass
    port_list = list(serial.tools.list_ports.comports())
    if len(port_list) == 0:
        print('未检测到串口,请确保设备已连接到电脑')
        time.sleep(1)
        GUI_Label1_SetText('设备未连接')
        GUI_Label1_SetBG('RED')
        Device_State = 0
        return
    My_MSN_Device = []
    My_MSN_Data = []
    for i in range(0, len(port_list)):
        try:
            ser = serial.Serial(port_list[i].name, 19200, timeout=2)
        except:
            print(port_list[i].name + '无法打开,请检查是否被其他程序占用')
            time.sleep(0.1)
            continue
        time.sleep(0.25)
        recv = SER_Read()
        if recv == 0:
            break
        recv = recv.decode('gbk')
        if len(recv) > 5:
            for n in range(0, len(recv) - 5):
                if ord(recv[n]) == 0 and recv[n + 1] == 'M' and recv[n + 2] == 'S' and recv[n + 3] == 'N' and '0' <= recv[n + 4] <= '9' and '0' <= recv[n + 5] <= '9':
                    My_MSN_Device.append(MSN_Device(port_list[i].name, (ord(recv[4]) - 48) * 10 + ord(recv[5]) - 48))
                    hex_code = int(0).to_bytes(1, 'little') + b'MSNCN'
                    SER_Write(hex_code)
                    time.sleep(0.25)
                    recv = SER_Read().decode('gbk')
                    if ord(recv[0]) == 0 and recv[1] == 'M' and recv[2] == 'S' and recv[3] == 'N' and recv[4] == 'C' and recv[5] == 'N':
                        print('MSN设备' + str(len(My_MSN_Device)) + '——' + port_list[i].name + '连接完成')
                    else:
                        print('MSN设备' + str(len(My_MSN_Device)) + '无法连接,请检查连接是否正常')
                    break
    print('MSN设备数量为' + str(len(My_MSN_Device)) + '个')
    if len(My_MSN_Device) >= 1:
        Device_State = 1
        State_change = 1
        Screen_Error = 0
        Read_M_SFR_Data(256)
        Print_MSN_Data()
        Read_MSN_Data(b'MSN_Status')
        UID = Read_MSN_Data(b'MSN_UID')
        Start_Thread1()
        ADC_det = Read_ADC_CH(9)
        ADC_det = (ADC_det + Read_ADC_CH(9)) / 2
        ADC_det = ADC_det - 125
        GUI_Label1_SetText('设备已连接')
        GUI_Label1_SetBG('GREEN')
        LCD_Change_now = 0
        GUI_Text_Clear()
        return
    # 未找到设备, 释放本次打开的串口
    Device_State = 0
    try:
        if ser is not None and ser.is_open:
            ser.close()
    except:
        pass


def MSN_Device_1_State_machine():
    """设备状态机: 处理按键、烧写请求、切页显示等。"""
    global Device_State, State_change, color_use, State_machine, key_on, key_eff, time_out
    global LCD_Change_now, LCD_Change_use, write_path1, write_path2, write_path3, write_path4
    if LCD_Change_now != LCD_Change_use:
        LCD_Change_now = LCD_Change_use
        LCD_State(LCD_Change_now)
        State_change = 1
    color_La = '#{:02x}{:02x}{:02x}'.format(S1_val * 8, S2_val * 4, S3_val * 8)
    GUI_Label2_SetBG(color_La)
    if write_path1 == 1:
        Write_Flash_hex_fast(3826, Img_data_use)
        write_path1 = 0
        State_change = 1
    if write_path2 == 1:
        Write_Flash_Photo_fast(0, photo_path2)
        write_path2 = 0
        State_change = 1
    if write_path3 == 1:
        Write_Flash_hex_fast(3926, Img_data_use)
        write_path3 = 0
        State_change = 1
    if write_path4 == 1:
        Write_Flash_hex_fast(0, Img_data_use)
        write_path4 = 0
        State_change = 1
    if time_out == 1:
        time_out = 0
        if Read_ADC_CH(9) < ADC_det:
            key_on = 1
        elif key_on == 1:
            key_eff = 1
            key_on = 0
        else:
            key_on = 0
        if key_eff == 1:
            key_eff = 0
            State_machine = State_machine + 1
            if State_machine > 5:
                State_machine = 0
            State_change = 1
            return
        return
    if State_machine == 0:
        show_gif()
        return
    if State_machine == 1:
        show_PC_state(BLUE, BLACK)
        return
    if State_machine == 2:
        color_now = S1_val * 2048 + S2_val * 32 + S3_val
        if color_now != color_use:
            color_use = color_now
            State_change = 1
        show_PC_state(color_use, BLACK)
        return
    if State_machine == 3:
        show_Photo1()
        return
    if State_machine == 4:
        show_PC_time()
        return
    if State_machine == 5:
        show_PC_Screen()
        return


# ---------------------------------------------------------------------------
# 主程序入口
# ---------------------------------------------------------------------------
print('该设备具有' + str(psutil.cpu_count(logical=False)) + '个内核和' + str(psutil.cpu_count()) + '个逻辑处理器')
print('该CPU主频为' + str(round(psutil.cpu_freq().current / 1000, 1)) + 'GHZ')
print('当前CPU占用率为' + str(psutil.cpu_percent()) + '%')
mem = psutil.virtual_memory()
print('该设备具有' + str(round(mem.total / 1073741824)) + 'GB的内存')
print('当前内存占用率为' + str(mem.percent) + '%')
print('开始运行时间' + datetime.fromtimestamp(psutil.boot_time()).strftime('%Y-%m-%d %H:%M:%S'))
battery = psutil.sensors_battery()
if battery != None:
    print('电池剩余电量' + str(battery.percent) + '%')

def Device_Loop():
    """设备状态机线程: 扫描/连接设备并驱动各页面显示。"""
    global D
    while True:
        D = D + 1
        if Device_State == 0:
            Get_MSN_Device()
            if Device_State == 0:
                time.sleep(1)
        elif Device_State == 1:
            MSN_Device_1_State_machine()


def main():
    global D, timer1, time_out, CPU, FC, BC, key_on, key_eff, State_change
    global gif_num, State_machine, Device_State, LCD_Change_use, LCD_Change_now
    global color_use, write_path1, write_path2, write_path3, write_path4
    global photo_path1, photo_path2, photo_path3, photo_path4
    global Thread1, S1_val, S2_val, S3_val

    D = 0
    timer1 = threading.Timer(0.2, TIM1)
    timer1.daemon = True
    time_out = 0
    CPU = 0
    FC = BLUE
    BC = BLACK
    key_on = 0
    key_eff = 0
    State_change = 1
    gif_num = 0
    State_machine = 5
    Device_State = 0
    LCD_Change_use = 0
    LCD_Change_now = 0
    color_use = RED
    write_path1 = 0
    write_path2 = 0
    write_path3 = 0
    write_path4 = 0
    photo_path1 = ''
    photo_path2 = ''
    photo_path3 = ''
    photo_path4 = ''
    S1_val = 31
    S2_val = 0
    S3_val = 0
    Thread1 = None

    timer1.start()

    app = QApplication(sys.argv)
    UI_Page()
    device_thread = threading.Thread(target=Device_Loop, daemon=True)
    device_thread.start()
    app.exec()
    os._exit(0)


if __name__ == '__main__':
    main()

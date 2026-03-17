# esp_st7789v2 commands

Quando `esp_st7789v2_init(log_enabled, true)` e usado, a lib registra:

- `AT+LCDCLR=<cor>`
- `AT+LCDPX=x,y,cor`
- `AT+LCDLINE=x0,y0,x1,y1,cor`
- `AT+LCDHL=x,y,width,0,cor`
- `AT+LCDVL=x,y,0,height,cor`
- `AT+LCDRECT=x,y,w,h,cor`
- `AT+LCDRRECT=x,y,w,h,r,cor`
- `AT+LCDFILL=x,y,w,h,cor`
- `AT+LCDFRRECT=x,y,w,h,r,cor`
- `AT+LCDGRID=x,y,w,h,cols,rows,cor`
- `AT+LCDCIRC=cx,cy,r,0,cor`
- `AT+LCDFCIRC=cx,cy,r,0,cor`
- `AT+LCDTRI=x0,y0,x1,y1,x2,y2,cor`
- `AT+LCDTXT=x,y,scale,fg,bg,text`

Exemplos:

```text
AT+LCDCLR=0x0000
AT+LCDPX=0,0,0xFFFF
AT+LCDLINE=0,0,319,169,0xFFFF
AT+LCDRECT=10,10,80,40,0xFFFF
AT+LCDRRECT=20,20,120,60,12,0xFFFF
AT+LCDFILL=20,20,40,20,0x06C0
AT+LCDFRRECT=160,20,120,60,16,0x06C0
AT+LCDGRID=10,10,300,150,10,5,0xFFFF
AT+LCDCIRC=160,85,40,0,0xFEC0
AT+LCDFCIRC=160,85,25,0,0x06C0
AT+LCDTRI=40,140,160,20,280,140,0x0018
AT+LCDTXT=10,10,2,0xFFFF,0x0000,HELLO
```

# esp_st7789v2 commands

Quando `esp_st7789v2_init(log_enabled, true)` e usado, a lib registra:

- `AT+LCDCLR=<cor>`
- `AT+LCDPX=x,y,cor`
- `AT+LCDHL=x,y,width,0,cor`
- `AT+LCDVL=x,y,0,height,cor`
- `AT+LCDRECT=x,y,w,h,cor`
- `AT+LCDFILL=x,y,w,h,cor`
- `AT+LCDGRID=x,y,w,h,cols,rows,cor`
- `AT+LCDTXT=x,y,scale,fg,bg,text`

Exemplos:

```text
AT+LCDCLR=0x0000
AT+LCDPX=0,0,0xFFFF
AT+LCDRECT=10,10,80,40,0xFFFF
AT+LCDFILL=20,20,40,20,0x06C0
AT+LCDGRID=10,10,300,150,10,5,0xFFFF
AT+LCDTXT=10,10,2,0xFFFF,0x0000,HELLO
```

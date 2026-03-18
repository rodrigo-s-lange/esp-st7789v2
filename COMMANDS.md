# esp_st7789v2 commands

When `esp_st7789v2_init(log_enabled, true)` is used, the component registers the commands below.

## Clear and pixel

`AT+LCDCLR=<color>`
- clears the full visible screen

`AT+LCDPX=x,y,color`
- draws one pixel at visible coordinates

## Lines and rectangles

`AT+LCDLINE=x0,y0,x1,y1,color`
- draws an arbitrary line

`AT+LCDHL=x,y,width,0,color`
- draws a horizontal line

`AT+LCDVL=x,y,0,height,color`
- draws a vertical line

`AT+LCDRECT=x,y,w,h,color`
- draws a rectangle outline

`AT+LCDRRECT=x,y,w,h,r,color`
- draws a rounded rectangle outline

`AT+LCDFILL=x,y,w,h,color`
- fills a rectangle

`AT+LCDFRRECT=x,y,w,h,r,color`
- fills a rounded rectangle

## Grid, circle and triangle

`AT+LCDGRID=x,y,w,h,cols,rows,color`
- draws a rectangular grid

`AT+LCDCIRC=cx,cy,r,0,color`
- draws a circle outline

`AT+LCDFCIRC=cx,cy,r,0,color`
- draws a filled circle

`AT+LCDTRI=x0,y0,x1,y1,x2,y2,color`
- draws a triangle outline

`AT+LCDFTRI=x0,y0,x1,y1,x2,y2,color`
- draws a filled triangle

## Bitmap text

`AT+LCDTXT=x,y,scale,fg,bg,text`
- draws `5x7` bitmap text

`AT+LCDBOX=x,y,w,h,scale,fg,bg,LEFT|CENTER|RIGHT,text`
- clears a fixed box and redraws bitmap text aligned inside it

## 7-segment text

`AT+LCD7SEG=x,y,height,thickness,fg,bg,text`
- draws 7-segment style text
- supported glyphs:
  - `0..9`
  - `A,B,C,D,E,F,G,H,I,J,L,N,P,R,S,T,U,Y`
  - `.`, `:`, `,`
  - degree aliases: `°`, `O`, `o`, `*`

`AT+LCD7BOX=x,y,w,h,t,fg,bg,LEFT|CENTER|RIGHT,text`
- clears a fixed box and redraws 7-segment text aligned inside it

## Progress bar

`AT+LCDBAR=x,y,w,h,min,max,value,border,fill,bg`
- draws a horizontal progress bar with border, background and proportional fill

## Examples

```text
AT+LCDCLR=0x0000
AT+LCDPX=0,0,0xFFFF
AT+LCDLINE=0,0,319,169,0xFFFF
AT+LCDRECT=10,10,80,40,0xFFFF
AT+LCDRRECT=20,20,120,60,12,0xFFFF
AT+LCDFILL=20,20,40,20,0x06C0
AT+LCDFRRECT=160,20,120,60,16,0x06C0
AT+LCDGRID=10,10,300,150,10,5,0x738E
AT+LCDCIRC=160,85,40,0,0xFEC0
AT+LCDFCIRC=160,85,25,0,0x06C0
AT+LCDTRI=40,140,160,20,280,140,0x0018
AT+LCDFTRI=40,140,160,20,280,140,0x0018
AT+LCDTXT=10,10,2,0xFFFF,0x0000,HELLO
AT+LCDBOX=10,10,120,30,2,0xFFFF,0x0000,CENTER,HELLO
AT+LCD7SEG=10,10,48,8,0xFFFF,0x0000,25°C
AT+LCD7BOX=10,10,150,48,8,0xFFFF,0x0000,RIGHT,25°C
AT+LCDBAR=10,10,200,20,0,100,75,0xFFFF,0x06C0,0x0000
```

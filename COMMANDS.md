# esp_st7789v2 commands

Quando `esp_st7789v2_init(log_enabled, true)` e usado, a lib registra os comandos abaixo.

## Limpeza e pixel

`AT+LCDCLR=<cor>`
- limpa a tela inteira com a cor informada
- se omitido, usa `BLACK`

Exemplo:
```text
AT+LCDCLR=0x0000
```

`AT+LCDPX=x,y,cor`
- desenha um pixel em `x,y`

Exemplo:
```text
AT+LCDPX=0,0,0xFFFF
```

## Linhas e retangulos

`AT+LCDLINE=x0,y0,x1,y1,cor`
- desenha linha arbitraria

`AT+LCDHL=x,y,width,0,cor`
- desenha linha horizontal

`AT+LCDVL=x,y,0,height,cor`
- desenha linha vertical

`AT+LCDRECT=x,y,w,h,cor`
- desenha retangulo de contorno

`AT+LCDRRECT=x,y,w,h,r,cor`
- desenha retangulo com bordas arredondadas

`AT+LCDFILL=x,y,w,h,cor`
- preenche retangulo

`AT+LCDFRRECT=x,y,w,h,r,cor`
- preenche retangulo arredondado

Exemplos:
```text
AT+LCDLINE=0,0,319,169,0xFFFF
AT+LCDRECT=10,10,80,40,0xFFFF
AT+LCDRRECT=20,20,120,60,12,0xFFFF
AT+LCDFILL=20,20,40,20,0x06C0
AT+LCDFRRECT=160,20,120,60,16,0x06C0
```

## Grade, circulo e triangulo

`AT+LCDGRID=x,y,w,h,cols,rows,cor`
- desenha grade retangular

`AT+LCDCIRC=cx,cy,r,0,cor`
- desenha circulo

`AT+LCDFCIRC=cx,cy,r,0,cor`
- desenha circulo preenchido

`AT+LCDTRI=x0,y0,x1,y1,x2,y2,cor`
- desenha triangulo

Exemplos:
```text
AT+LCDGRID=10,10,300,150,10,5,0xFFFF
AT+LCDCIRC=160,85,40,0,0xFEC0
AT+LCDFCIRC=160,85,25,0,0x06C0
AT+LCDTRI=40,140,160,20,280,140,0x0018
```

## Texto bitmap

`AT+LCDTXT=x,y,scale,fg,bg,text`
- desenha texto `5x7`
- `scale` multiplica tamanho da fonte

Exemplo:
```text
AT+LCDTXT=10,10,2,0xFFFF,0x0000,HELLO
```

`AT+LCDBOX=x,y,w,h,scale,fg,bg,LEFT|CENTER|RIGHT,text`
- limpa a caixa
- desenha o texto alinhado dentro da area
- pensado para atualizacao local sem flicker

Exemplo:
```text
AT+LCDBOX=10,10,120,30,2,0xFFFF,0x0000,CENTER,HELLO
```

## Texto 7 segmentos

`AT+LCD7SEG=x,y,height,thickness,fg,bg,text`
- desenha texto em estilo `7seg`
- suporte atual:
  - `0..9`
  - `A,B,C,D,E,F,G,H,I,J,L,N,P,R,S,T,U,Y`
  - `.`
  - `:`
  - `,`
  - `grau`
- para grau:
  - `°`
  - `O`
  - `o`
  - `*`

Exemplos:
```text
AT+LCD7SEG=10,10,48,8,0xFFFF,0x0000,12:34
AT+LCD7SEG=10,70,42,6,0xFEC0,0x0000,25.4
AT+LCD7SEG=10,120,36,5,0x07FF,0x0000,25°C
```

## Barra de progresso

`AT+LCDBAR=x,y,w,h,min,max,value,border,fill,bg`
- desenha barra de progresso horizontal
- borda de `1 px`
- preenchimento proporcional ao intervalo `[min,max]`

Exemplos:
```text
AT+LCDBAR=10,10,200,20,0,100,75,0xFFFF,0x06C0,0x0000
AT+LCDBAR=10,40,200,20,0,100,25,0xFFFF,0xF400,0x0000
AT+LCDBAR=10,70,280,16,0,1000,630,0x738E,0x07FF,0x0000
```

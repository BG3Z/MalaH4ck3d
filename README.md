# MalaH4ck3d

Proyecto Flipper Zero NFC para gestionar saldo y cargar tarjetas con una interfaz visual personalizada.

## Descripción

`MalaH4ck3d` es una aplicación externa para Flipper Zero desarrollada en C con el SDK de Flipper. Proporciona una interfaz de menú y varias pantallas para leer saldo NFC, ingresar una clave, recargar saldo y mostrar información adicional. El proyecto también incluye una animación de splash con un tren y humo.

## Características

- Menú principal con opciones:
  - Check Balance
  - Insert your KEY
  - Top up Balance
  - Disclaimer
  - Credits
- Lectura de saldo desde bloque NFC usando MF Classic.
- Entrada de clave en formato hexadecimal de 6 bytes.
- Recarga de saldo con control por incremento entero (`+1` / `-1`) y límite de 0 a 100 EUR.
- UI con bordes y texto claro en cada sección.
- Splash animado con tren y vapor.
- Icono de aplicación en `images/calavera.png`.

## Estructura del proyecto

- `consorcio.c` - Código fuente principal de la aplicación.
- `application.fam` - Archivo de metadatos de la app para Flipper, con icono y categoría.
- `images/` - Carpeta de assets con icono y posibles imágenes adicionales.
- `README.md` - Esta documentación.

## Uso

1. Clona o copia el repositorio en tu carpeta de proyecto.
2. Asegúrate de tener configurado el entorno de Flipper SDK y `ufbt`.
3. Compila y lanza la app con:

```powershell
ufbt launch
```

> Si `ufbt launch` no está disponible, revisa tu instalación del SDK de Flipper y el script de `ufbt`.

## Personalización

- Modifica `consorcio.c` para ajustar la interfaz, posiciones de texto o animación.
- Cambia `application.fam` si quieres renombrar la app, cambiar el icono o la categoría.
- El icono actual está en `images/calavera.png`.

## Notas importantes

- El proyecto asume el uso de tarjetas MF Classic y acceso por clave.
- Las pantallas se dibujan con funciones de canvas del SDK.
- El texto `€` puede depender del soporte de la fuente del dispositivo.

## Licencia

Este repositorio no incluye una licencia explícita. Añade una licencia si quieres compartirlo públicamente.


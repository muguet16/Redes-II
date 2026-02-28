import sys
import urllib.parse

# 1. Leemos los argumentos que nos manda el servidor en C por STDIN
entrada = sys.stdin.read().strip()

# 2. Parseamos la cadena (convierte "nombre=Marcos" en un diccionario)
argumentos = urllib.parse.parse_qs(entrada)

# 3. Obtenemos el valor de 'nombre' (si no existe, ponemos 'Mundo' por defecto)
nombre = argumentos.get('nombre', ['Mundo'])[0]

# 4. Imprimimos el resultado en formato HTML
print("<html><body>")
print(f"<h1>Hola {nombre}!</h1>")
print("</body></html>")
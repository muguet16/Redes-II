import sys
import urllib.parse

# 1. Leemos los argumentos por STDIN
entrada = sys.stdin.read().strip()

# 2. Parseamos la cadena (ej: "temperatura=30")
argumentos = urllib.parse.parse_qs(entrada)

print("<html><body>")
print("<h2>Conversor de Temperatura</h2>")

try:
    # 3. Extraemos la temperatura y hacemos el cálculo
    celsius_str = argumentos.get('temperatura', ['0'])[0]
    celsius = float(celsius_str)
    fahrenheit = (celsius * 9/5) + 32
    
    # 4. Imprimimos el resultado
    print(f"<p>{celsius} grados Celsius equivalen a <b>{fahrenheit} grados Fahrenheit</b>.</p>")
except ValueError:
    print("<p style='color:red;'>Error: Por favor, introduce un número válido para la temperatura.</p>")

print("</body></html>")
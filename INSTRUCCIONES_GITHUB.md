# Instrucciones para subir el código a GitHub

## Opción 1: Usar el script (recomendado)

1. Ve a: https://github.com/settings/tokens
2. Crea un nuevo token con scope 'repo' (Full control)
3. Copia el token (deberá empezar por ghp_)
4. Ejecuta:

   ```
   cd D:\timeres
   .\push-to-github.ps1 ghp_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
   ```

## Opción 2: Manual

1. Crea el repositorio en GitHub:
   - Ve a: https://github.com/new
   - Nombre: windows-timer-resolution-stressced-version
   - Click en "Create repository"

2. Copia la URL SSH del repositorio

3. Ejecuta:
   ```
   cd D:\timeres
   git remote add origin git@github.com:carlosedt/windows-timer-resolution-stressced-version.git
   git push -u origin master
   ```

## Archivos que se subirán
- timeres.c - Código fuente
- timeres.rc - Layout del diálogo
- app.manifest - Configuración de permisos
- push-to-github.ps1 - Script de subida
- .gitignore - Archivos a ignorar
- README.md - Documentación
- AGENTS.md - Instrucciones para agentes
- timeres.exe - Ejecutable final

# Guía para subir el código a GitHub

## Paso 1: Crear el repositorio en GitHub

1. Ve a: https://github.com/new
2. Crea un nuevo repositorio con nombre: windows-timer-resolution-stressced-version
3. Haz clic en "Create repository"

## Paso 2: Configurar el repositorio local

```
cd D:\timeres
git remote add origin https://github.com/tu-usuario/windows-timer-resolution-stressced-version.git
```

## Paso 3: Subir el código

```
git push -u origin master
```

## Alternativa: Usar el script

```
cd D:\timeres
.\push-to-github.ps1 TU_TOKEN
```

Donde TU_TOKEN es un token personal de GitHub (puedes crear uno en: https://github.com/settings/tokens)

El token debe tener el scope 'repo' (Control repositories)

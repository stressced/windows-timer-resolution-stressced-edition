# Script para crear y subir el repositorio a GitHub
# Uso: .\push-to-github.ps1 TU_TOKEN

if ($args.Count -ne 1) {
    Write-Host "Uso: .\push-to-github.ps1 TU_TOKEN"
    Write-Host "Ejemplo: .\push-to-github.ps1 ghp_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
    Write-Host ""
    Write-Host "Para crear el token:"
    Write-Host "1. Ve a: https://github.com/settings/tokens"
    Write-Host "2. Crea un nuevo token con scope 'repo' (Control repositories)"
    exit 1
}

$token = $args[0]
$repoName = "windows-timer-resolution-stressced-version"

Write-Host "Creando repositorio $repoName en GitHub..."

$headers = @{
    "Authorization" = "token $token"
    "Accept" = "application/vnd.github.v3+json"
    "Content-Type" = "application/json"
}

$body = @{
    name = $repoName
    description = "Timer Resolution Tool - Stressced Version for Windows"
    private = $false
}

try {
    $response = Invoke-RestMethod -Uri "https://api.github.com/user/repos" -Method POST -Headers $headers -Body $body | ConvertTo-Json
    
    if ($response.Contains("error")) {
        Write-Host "Error al crear el repositorio:"
        Write-Host $response
        exit 1
    }
    
    Write-Host "Repositorio creado exitosamente!"
    
    $repoUrl = $response | ConvertFrom-Json | Select-Object -ExpandProperty ssh_url
    
    Write-Host "Configurando remote origin..."
    git remote add origin $repoUrl
    
    Write-Host "Subiendo código..."
    git push -u origin master
    
    Write-Host "Repositorio creado y código subido exitosamente!"
    Write-Host "URL: $repoUrl"
} catch {
    Write-Host "Error: $_"
}

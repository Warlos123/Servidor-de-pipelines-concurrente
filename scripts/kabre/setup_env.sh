#!/usr/bin/env bash
#
# setup_env.sh: Configura el entorno en Kabré (CeNAT)
# Carga módulos necesarios e instala libyaml en $HOME/.local si no existe en el sistema.
#
set -euo pipefail

echo "=================================================="
echo "  Configuración de entorno para Kabré (CeNAT)     "
echo "=================================================="

# 1. Cargar módulos de entorno si 'module' está disponible
if command -v module &>/dev/null || type module &>/dev/null; then
    echo "[+] Cargando módulos de HPC..."
    # Intentar cargar una versión moderna de GCC (11+)
    if module avail gcc 2>&1 | grep -q "gcc/11"; then
        module load gcc/11* 2>/dev/null || true
    elif module avail gcc 2>&1 | grep -q "gcc/"; then
        module load gcc 2>/dev/null || true
    fi

    # Cargar Python 3 si está como módulo
    if module avail python 2>&1 | grep -q "python/3"; then
        module load python/3* 2>/dev/null || true
    elif module avail python 2>&1 | grep -q "python"; then
        module load python 2>/dev/null || true
    fi
fi

echo "[i] Compilador C detectado: $(gcc --version 2>/dev/null | head -n1 || echo 'No encontrado')"
echo "[i] Python detectado: $(python3 --version 2>/dev/null || echo 'No encontrado')"

# 2. Configurar rutas de $HOME/.local
mkdir -p "$HOME/.local/include" "$HOME/.local/lib" "$HOME/.local/bin"

export CPATH="$HOME/.local/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$HOME/.local/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$HOME/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$HOME/.local/bin:$PATH"

# 3. Comprobar si libyaml está disponible
echo "[+] Comprobando libyaml..."
LIBYAML_OK=0
if echo '#include <yaml.h>' | gcc -E - &>/dev/null; then
    echo "[✓] libyaml ya está disponible en el sistema o en \$HOME/.local."
    LIBYAML_OK=1
fi

# 4. Instalar libyaml si hace falta
if [ "$LIBYAML_OK" -eq 0 ]; then
    echo "[!] libyaml no encontrada en rutas estándar. Procediendo a compilarla en $HOME/.local..."
    BUILD_DIR=$(mktemp -d /tmp/libyaml-build.XXXXXX)
    cd "$BUILD_DIR"

    YAML_VER="0.2.5"
    TARBALL="yaml-${YAML_VER}.tar.gz"
    URL="https://github.com/yaml/libyaml/releases/download/${YAML_VER}/${TARBALL}"

    echo "[+] Descargando libyaml ${YAML_VER}..."
    if command -v wget &>/dev/null; then
        wget -q "$URL" -O "$TARBALL"
    elif command -v curl &>/dev/null; then
        curl -fsSL "$URL" -o "$TARBALL"
    else
        echo "[-] Error: no se encontró wget ni curl. Descarga libyaml manualmente."
        exit 1
    fi

    tar -xzf "$TARBALL"
    cd "yaml-${YAML_VER}"

    echo "[+] Configurando e instalando en $HOME/.local..."
    ./configure --prefix="$HOME/.local" --disable-static >/dev/null
    make -j"$(nproc 2>/dev/null || echo 2)" >/dev/null
    make install >/dev/null

    cd "$HOME"
    rm -rf "$BUILD_DIR"
    echo "[✓] libyaml instalada exitosamente en $HOME/.local."
fi

# 5. Agregar exportaciones al .bashrc si no están presentes
BASHRC="$HOME/.bashrc"
if [ -f "$BASHRC" ]; then
    if ! grep -q "KABRE_PIPELINE_ENV" "$BASHRC"; then
        echo "[+] Añadiendo variables de entorno a $BASHRC..."
        cat << 'EOF' >> "$BASHRC"

# >>> KABRE_PIPELINE_ENV >>>
export CPATH="$HOME/.local/include${CPATH:+:$CPATH}"
export LIBRARY_PATH="$HOME/.local/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$HOME/.local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$HOME/.local/bin:$PATH"
# <<< KABRE_PIPELINE_ENV <<<
EOF
        echo "[✓] .bashrc actualizado."
    fi
fi

echo "=================================================="
echo "[✓] Entorno listo. Ahora puedes ejecutar tus pruebas."
echo "=================================================="

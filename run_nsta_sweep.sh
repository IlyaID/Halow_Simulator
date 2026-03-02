#!/usr/bin/env bash
set -e

# Каталог с бинарником
BUILD_DIR="build"
BIN="$BUILD_DIR/halow-sim"

# Базовый scenario.json (будет копироваться и модифицироваться)
BASE_SCENARIO="scenario_base.json"

# Список чисел станций
NSTAS=("5" "10" "20" "40" "80" "120")

# Проверка
if [ ! -f "$BIN" ]; then
  echo "Binary $BIN not found, build first"
  exit 1
fi

if [ ! -f "$BASE_SCENARIO" ]; then
  echo "Base scenario $BASE_SCENARIO not found"
  echo "Создай $BASE_SCENARIO на основе своего scenario.json (без поля nSta или с любым nSta)"
  exit 1
fi

mkdir -p runs

for N in "${NSTAS[@]}"; do
  RUN_DIR="runs/nsta_${N}"
  mkdir -p "$RUN_DIR"

  # Сформировать scenario.json для этого прогона
  # Предполагаем, что в BASE_SCENARIO есть поле "nSta"
  # Если jq установлен:
  jq ".nSta = ${N}" "$BASE_SCENARIO" > "${RUN_DIR}/scenario.json"

  echo "=== Run for nSta=${N} ==="
  # Запускаем из build, а scenario.json читаем как ../scenario.json
  pushd "$BUILD_DIR" > /dev/null

  # Копируем scenario.json в корень (../scenario.json относительно build)
  cp "../${RUN_DIR}/scenario.json" "../scenario.json"

  ./halow-sim

  # Переносим результаты в RUN_DIR
  mv ../summary.csv "../${RUN_DIR}/summary.csv"
  mv ../results.csv "../${RUN_DIR}/results.csv"
  mv ../energy_per_node.csv "../${RUN_DIR}/energy_per_node.csv"
  mv ../throughput_per_node.csv "../${RUN_DIR}/throughput_per_node.csv"

  popd > /dev/null
done

echo "Все прогоны завершены. Результаты в каталоге runs/."


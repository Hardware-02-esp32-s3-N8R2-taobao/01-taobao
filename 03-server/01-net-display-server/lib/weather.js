// 天气模块 — Open-Meteo API 封装，含本地缓存
// 从 server.js 中拆分，降低主文件耦合度

const WEATHER_URL =
  "https://api.open-meteo.com/v1/forecast?latitude=30.25&longitude=119.75&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&current=temperature_2m,relative_humidity_2m,weather_code&timezone=Asia%2FShanghai&forecast_days=5";
const WEATHER_CACHE_MS = 30 * 60 * 1000;

let weatherCache = {
  expiresAt: 0,
  data: null
};

function getWeatherDescription(code) {
  const codeMap = {
    0: "晴朗",
    1: "大致晴",
    2: "多云",
    3: "阴天",
    45: "有雾",
    48: "冻雾",
    51: "毛毛雨",
    53: "小毛雨",
    55: "中毛雨",
    56: "冻毛雨",
    57: "强冻毛雨",
    61: "小雨",
    63: "中雨",
    65: "大雨",
    66: "冻雨",
    67: "强冻雨",
    71: "小雪",
    73: "中雪",
    75: "大雪",
    77: "雪粒",
    80: "阵雨",
    81: "较强阵雨",
    82: "强阵雨",
    85: "阵雪",
    86: "强阵雪",
    95: "雷暴",
    96: "雷暴夹小冰雹",
    99: "雷暴夹大冰雹"
  };

  return codeMap[code] || "花园天气保密中";
}

async function getWeatherForecast() {
  if (weatherCache.data && weatherCache.expiresAt > Date.now()) {
    return weatherCache.data;
  }

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 8000);
  let response;
  try {
    response = await fetch(WEATHER_URL, { signal: controller.signal });
  } finally {
    clearTimeout(timer);
  }
  if (!response.ok) {
    throw new Error(`weather request failed: ${response.status}`);
  }

  const payload = await response.json();
  const daily = payload.daily || {};
  const current = payload.current || {};
  const forecast = (daily.time || []).map((date, index) => ({
    date,
    weatherCode: daily.weather_code?.[index] ?? null,
    weatherText: getWeatherDescription(daily.weather_code?.[index]),
    tempMax: daily.temperature_2m_max?.[index] ?? null,
    tempMin: daily.temperature_2m_min?.[index] ?? null,
    rainProbability: daily.precipitation_probability_max?.[index] ?? null
  }));

  const data = {
    location: "杭州临安区",
    source: "Open-Meteo",
    current: {
      temperature: current.temperature_2m ?? null,
      humidity: current.relative_humidity_2m ?? null,
      weatherCode: current.weather_code ?? null,
      weatherText: getWeatherDescription(current.weather_code),
      time: current.time || new Date().toISOString()
    },
    forecast,
    updatedAt: new Date().toISOString()
  };

  weatherCache = {
    data,
    expiresAt: Date.now() + WEATHER_CACHE_MS
  };

  return data;
}

function invalidateWeatherCache() {
  weatherCache.expiresAt = 0;
}

module.exports = { getWeatherForecast, invalidateWeatherCache };

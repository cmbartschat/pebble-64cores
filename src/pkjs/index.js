var Clay = require('pebble-clay')
var clayConfig = require('./config')
var clay = new Clay(clayConfig)
const baseX = require('base-x').default

const {encode, decode} = baseX('0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ')

const rotate = (array, n) => array.slice(n).concat(array.slice(0, n))

var fetch = function (url, type, callback) {
  var xhr = new XMLHttpRequest()
  xhr.onload = function () {
    callback(this.responseText)
  }
  xhr.open(type, url)
  xhr.send()
}

const decodeShortId = (id) => {
  const arr = decode(id)
  const hexArray = Array.from(arr).map((e) => e.toString(16).padStart(2, '0'))
  return rotate(hexArray, -4).join('')
}

let gameId = localStorage.getItem('game-id')

const setGameId = (v) => {
  console.log('setting game ID to', v)

  if (gameId !== v) {
    gameId = v
    localStorage.setItem('game-id', v || '')
    localStorage.removeItem('latest_data_date')
    refresh()
  }
}

const sendState = (data) => {
  Pebble.sendAppMessage({
    TYPE: 'STATE',
    LEVELS: data.levels,
    BROKEN: (data.broken || []).map((e) => (e ? 1 : 0)),
    CYCLES: Math.floor(data.cycles / 64000),
  })
}

const refresh = () => {
  console.log('refreshing')
  if (!gameId) {
    Pebble.sendAppMessage({TYPE: 'RESET'})
    return
  }

  const savedDate = localStorage.getItem('latest_data_date')
  if (savedDate) {
    const expiration = new Date(savedDate)
    expiration.setMinutes(expiration.getMinutes() + 5)
    if (new Date() < expiration) {
      sendState(JSON.parse(localStorage.getItem('latest_data')))
      return
    }
  }

  fetch('https://www.64cor.es/api/public/' + gameId, 'GET', (dataString) => {
    const data = JSON.parse(dataString)
    if (data.levels) {
      localStorage.setItem('latest_data', dataString)
      localStorage.setItem('latest_data_date', String(new Date()))
      sendState(data)
    }
  })
}

// override send config, in case want to modify (change types) values or debug
Pebble.addEventListener('webviewclosed', function (e) {
  if (e && !e.response) {
    return
  }

  const newGameUrl = JSON.parse(e.response).GAME_URL.value

  console.log(JSON.stringify(newGameUrl))

  if (!newGameUrl || typeof newGameUrl !== 'string') {
    setGameId(null)
  } else {
    const shortId = newGameUrl.split('/').findLast(Boolean)
    setGameId(decodeShortId(shortId))
  }
})

Pebble.addEventListener('appmessage', function (e) {
  console.log('got app message', e.payload)
  if (e.payload.TYPE === 'REFRESH') {
    refresh()
  }
})

refresh()

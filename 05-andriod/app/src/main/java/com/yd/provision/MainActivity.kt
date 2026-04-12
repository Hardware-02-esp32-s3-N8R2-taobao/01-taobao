package com.yd.provision

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.LinkProperties
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.espressif.provisioning.DeviceConnectionEvent
import com.espressif.provisioning.ESPConstants
import com.espressif.provisioning.ESPDevice
import com.espressif.provisioning.ESPProvisionManager
import com.espressif.provisioning.WiFiAccessPoint
import com.espressif.provisioning.listeners.ProvisionListener
import org.greenrobot.eventbus.EventBus
import org.greenrobot.eventbus.Subscribe
import org.greenrobot.eventbus.ThreadMode

class MainActivity : AppCompatActivity() {

    companion object {
        private const val TAG = "YDProvision"
    }

    private lateinit var boardWifiText: TextView
    private lateinit var homeSsidInput: EditText
    private lateinit var homePasswordInput: EditText
    private lateinit var statusText: TextView

    private var espDevice: ESPDevice? = null
    private var pendingHomeSsid: String = ""
    private var pendingHomePassword: String = ""
    private var currentBoardApName: String = ""
    private var isProvisioningInProgress: Boolean = false
    private var boundBoardNetwork: Network? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        boardWifiText = findViewById(R.id.boardWifiText)
        homeSsidInput = findViewById(R.id.homeSsidInput)
        homePasswordInput = findViewById(R.id.homePasswordInput)
        statusText = findViewById(R.id.statusText)

        ensurePermissions()
        refreshCurrentBoardWifi()

        findViewById<Button>(R.id.refreshBoardWifiButton).setOnClickListener {
            refreshCurrentBoardWifi()
        }
        findViewById<Button>(R.id.fillCurrentWifiButton).setOnClickListener {
            fillCurrentWifiName()
        }
        findViewById<Button>(R.id.sendWifiButton).setOnClickListener {
            startProvisioning()
        }
    }

    override fun onStart() {
        super.onStart()
        EventBus.getDefault().register(this)
        refreshCurrentBoardWifi()
    }

    override fun onStop() {
        releaseBoardNetworkBinding()
        EventBus.getDefault().unregister(this)
        super.onStop()
    }

    @Subscribe(threadMode = ThreadMode.MAIN)
    fun onDeviceConnectionEvent(event: DeviceConnectionEvent) {
        when (event.eventType) {
            ESPConstants.EVENT_DEVICE_CONNECTED -> {
                if (isProvisioningInProgress) {
                    updateStatus("已经连上板子热点，正在发送家里 Wi‑Fi 信息…")
                    espDevice?.provision(pendingHomeSsid, pendingHomePassword, provisionListener)
                }
            }
            ESPConstants.EVENT_DEVICE_CONNECTION_FAILED -> {
                finishProvisioning()
                updateStatus("无法和板子通信。请确认手机当前连接的是板子热点 YD-PROV-*，然后再重试。")
            }
            ESPConstants.EVENT_DEVICE_DISCONNECTED -> {
                if (isProvisioningInProgress) {
                    updateStatus("和板子的连接已断开。若板子已经切到家里 Wi‑Fi，这属于正常现象。")
                }
            }
        }
    }

    private val provisionListener = object : ProvisionListener {
        override fun createSessionFailed(e: Exception?) {
            finishProvisioning()
            updateStatus("建立会话失败：${e?.message ?: "unknown"}")
        }

        override fun wifiConfigSent() {
            updateStatus("家里 Wi‑Fi 信息已发送给板子。")
        }

        override fun wifiConfigFailed(e: Exception?) {
            finishProvisioning()
            updateStatus("发送 Wi‑Fi 信息失败：${e?.message ?: "unknown"}")
        }

        override fun wifiConfigApplied() {
            updateStatus("板子已应用 Wi‑Fi 信息，正在尝试联网…")
        }

        override fun wifiConfigApplyFailed(e: Exception?) {
            finishProvisioning()
            updateStatus("板子应用 Wi‑Fi 信息失败：${e?.message ?: "unknown"}")
        }

        override fun provisioningFailedFromDevice(failureReason: ESPConstants.ProvisionFailureReason?) {
            finishProvisioning()
            updateStatus("板子连接家里 Wi‑Fi 失败：${failureReason ?: ESPConstants.ProvisionFailureReason.UNKNOWN}")
        }

        override fun deviceProvisioningSuccess() {
            finishProvisioning()
            updateStatus("配网成功。板子已保存家里 Wi‑Fi，后续重启会自动连接。")
        }

        override fun onProvisioningFailed(e: Exception?) {
            finishProvisioning()
            updateStatus("配网失败：${e?.message ?: "unknown"}")
        }
    }

    private fun startProvisioning() {
        refreshCurrentBoardWifi()
        val homeSsid = homeSsidInput.text.toString().trim()
        val homePassword = homePasswordInput.text.toString()

        if (currentBoardApName.isBlank()) {
            updateStatus("当前没有连接板子热点。请先到系统 Wi‑Fi 中连接 YD-PROV-*，再回来点发送。")
            return
        }
        if (homeSsid.isEmpty()) {
            updateStatus("请先填写家里 Wi‑Fi 名称。")
            return
        }

        pendingHomeSsid = homeSsid
        pendingHomePassword = homePassword
        isProvisioningInProgress = true

        if (!bindProcessToBoardWifi()) {
            finishProvisioning()
            updateStatus("已连上板子热点，但 Android 没把通信切到这个 Wi‑Fi。请关闭 VPN / 智能网络切换后重试。")
            return
        }

        val provisionManager = ESPProvisionManager.getInstance(applicationContext)
        espDevice = provisionManager.createESPDevice(
            ESPConstants.TransportType.TRANSPORT_SOFTAP,
            ESPConstants.SecurityType.SECURITY_0
        ).apply {
            setDeviceName(currentBoardApName)
            setWifiDevice(WiFiAccessPoint().apply {
                wifiName = currentBoardApName
                password = "gf666666"
            })
        }

        updateStatus("已检测到板子热点 $currentBoardApName，正在和板子建立通信…")
        espDevice?.connectWiFiDevice()
    }

    private fun ensurePermissions() {
        val permissions = mutableListOf(
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.ACCESS_WIFI_STATE,
            Manifest.permission.CHANGE_WIFI_STATE,
            Manifest.permission.ACCESS_NETWORK_STATE
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            permissions += Manifest.permission.NEARBY_WIFI_DEVICES
        }

        val missing = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            ActivityCompat.requestPermissions(this, missing.toTypedArray(), 1001)
        }
    }

    private fun fillCurrentWifiName() {
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val ssid = wifiManager.connectionInfo?.ssid?.trim('"').orEmpty()
        if (ssid.isNotEmpty() && !ssid.startsWith("YD-PROV-")) {
            homeSsidInput.setText(ssid)
            updateStatus("已填入当前手机连接的 Wi‑Fi：$ssid")
        } else {
            updateStatus("当前手机连接的是板子热点。请手动填写家里 Wi‑Fi 名称。")
        }
    }

    private fun refreshCurrentBoardWifi() {
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val ssid = wifiManager.connectionInfo?.ssid?.trim('"').orEmpty()
        if (ssid.startsWith("YD-PROV-")) {
            currentBoardApName = ssid
            boardWifiText.text = "当前已连接板子热点：$ssid"
        } else {
            currentBoardApName = ""
            boardWifiText.text = "当前未连接板子热点"
        }
    }

    private fun finishProvisioning() {
        isProvisioningInProgress = false
        releaseBoardNetworkBinding()
    }

    private fun updateStatus(text: String) {
        statusText.text = "状态：$text"
    }

    private fun bindProcessToBoardWifi(): Boolean {
        val connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val activeNetwork = connectivityManager.activeNetwork
        val candidateNetworks = linkedSetOf<Network>()

        if (activeNetwork != null) {
            val activeCapabilities = connectivityManager.getNetworkCapabilities(activeNetwork)
            if (activeCapabilities?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true) {
                candidateNetworks += activeNetwork
                Log.d(TAG, "Active network is Wi-Fi, trying it first.")
            } else {
                Log.d(TAG, "Active network is not Wi-Fi: $activeNetwork")
            }
        }

        connectivityManager.allNetworks
            .filter { network ->
                connectivityManager.getNetworkCapabilities(network)
                    ?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true
            }
            .sortedByDescending { network ->
                val capabilities = connectivityManager.getNetworkCapabilities(network)
                when {
                    capabilities == null -> 0
                    networkMatchesCurrentBoard(capabilities) -> 3
                    networkLooksLikeBoardSoftAp(connectivityManager.getLinkProperties(network)) -> 2
                    else -> 1
                }
            }
            .forEach { candidateNetworks += it }

        Log.d(TAG, "Wi-Fi candidates for binding: ${candidateNetworks.joinToString()}")

        for (network in candidateNetworks) {
            val capabilities = connectivityManager.getNetworkCapabilities(network)
            val linkProperties = connectivityManager.getLinkProperties(network)
            Log.d(
                TAG,
                "Trying bind on network=$network, caps=$capabilities, link=$linkProperties"
            )
            if (connectivityManager.bindProcessToNetwork(network)) {
                boundBoardNetwork = network
                Log.d(TAG, "bindProcessToNetwork succeeded on $network")
                return true
            }
        }

        Log.w(TAG, "bindProcessToNetwork failed for all Wi-Fi candidates")
        return false
    }

    private fun releaseBoardNetworkBinding() {
        if (boundBoardNetwork == null) {
            return
        }
        val connectivityManager = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        connectivityManager.bindProcessToNetwork(null)
        boundBoardNetwork = null
    }

    private fun networkMatchesCurrentBoard(capabilities: NetworkCapabilities): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            return true
        }
        val wifiInfo = capabilities.transportInfo as? android.net.wifi.WifiInfo ?: return false
        val ssid = wifiInfo.ssid?.trim('"').orEmpty()
        return ssid == currentBoardApName
    }

    private fun networkLooksLikeBoardSoftAp(linkProperties: LinkProperties?): Boolean {
        if (linkProperties == null) {
            return false
        }

        val hasBoardGateway = linkProperties.routes.any { route ->
            val gateway = route.gateway?.hostAddress.orEmpty()
            gateway == "192.168.4.1"
        }
        if (hasBoardGateway) {
            return true
        }

        return linkProperties.linkAddresses.any { address ->
            val hostAddress = address.address.hostAddress.orEmpty()
            hostAddress.startsWith("192.168.4.")
        }
    }
}

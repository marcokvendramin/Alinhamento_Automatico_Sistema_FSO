<table width="100%">
  <tr>
    <td width="15%" align="left">🇧🇷</td>
    <td width="70%" align="center">
      <h1>Arquitetura de Comunicação Óptica via Laser</h1>
    </td>
    <td width="15%"></td>
  </tr>
</table>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Este projeto apresenta um sistema de comunicação óptica bidirecional de médio alcance baseado em <strong>FSO — Free Space Optics</strong>, integrando hardware e software para a transmissão de dados por meio de um feixe de laser.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;O sistema utiliza dois microcontroladores <strong>Arduino Nano ESP32</strong>, configurados como pontos de acesso Wi-Fi e responsáveis por hospedar interfaces web. Por meio delas, o usuário pode enviar mensagens de texto e arquivos de imagem, além de visualizar e monitorar o conteúdo recebido.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;A transmissão ocorre pela modulação digital do laser, na qual os bits <strong>0</strong> e <strong>1</strong> são representados, respectivamente, pelos estados desligado e ligado. Os dados são enviados sequencialmente, detectados por um fototransistor e reconstruídos pelo dispositivo receptor.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;O enlace óptico alcançou <strong>37,4 metros sem perda de pacotes</strong>. Nesse limite, a atenuação e a divergência do feixe afetam diretamente a relação sinal-ruído, exigindo alinhamento óptico preciso e um protocolo resiliente para detectar, sincronizar e reconstruir os dados mesmo com o enfraquecimento do sinal.
</p>

<br>

<!-- Substitua o caminho abaixo pelo nome da sua imagem -->

<p align="center">
  <img
    src="images/sistema-fso-01.jpg"
    alt="Sistema de comunicação óptica via laser"
    width="850"
  />
</p>

<p align="center">
  <em>Figura 1 — Estrutura do sistema de comunicação óptica.</em>
</p>

<br>

<!-- Espaço para uma segunda imagem ou GIF -->

<p align="center">
  <img
    src="images/sistema-fso-02.gif"
    alt="Demonstração do sistema FSO em funcionamento"
    width="850"
  />
</p>

<p align="center">
  <em>Figura 2 — Transmissão e recepção de dados durante a operação.</em>
</p>

<br>

<div align="center">

## 🎥 Demonstração prática

Veja o sistema operando e transmitindo dados em uma demonstração curta:

### [▶ Assistir ao vídeo no YouTube](https://www.youtube.com/watch?v=WR5zdiHbGd0)

</div>

---

<table width="100%">
  <tr>
    <td width="15%" align="left">🇺🇸</td>
    <td width="70%" align="center">
      <h1>Laser-Based Optical Communication Architecture</h1>
    </td>
    <td width="15%"></td>
  </tr>
</table>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;This project presents a medium-range bidirectional optical communication system based on <strong>FSO — Free-Space Optics</strong>, integrating hardware and software for data transmission through a laser beam.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;The system uses two <strong>Arduino Nano ESP32</strong> microcontrollers configured as Wi-Fi access points and web interface hosts. Through these interfaces, users can send text messages and image files while viewing and monitoring the received content.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Transmission is performed through digital modulation of the laser beam, with bits <strong>0</strong> and <strong>1</strong> represented by the laser's off and on states, respectively. The data is transmitted sequentially, detected by a phototransistor, and reconstructed by the receiving device.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;The optical link reached <strong>37.4 meters with no packet loss</strong>. At this range, beam attenuation and divergence directly affect the signal-to-noise ratio, requiring precise optical alignment and a resilient protocol to detect, synchronize, and reconstruct the data even as the signal weakens.
</p>

<br>

<!-- Replace the path below with your image filename -->

<p align="center">
  <img
    src="images/fso-system-01.jpg"
    alt="Laser-based optical communication system"
    width="850"
  />
</p>

<p align="center">
  <em>Figure 1 — Optical communication system architecture.</em>
</p>

<br>

<!-- Space for a second image or GIF -->

<p align="center">
  <img
    src="images/fso-system-02.gif"
    alt="FSO system operating demonstration"
    width="850"
  />
</p>

<p align="center">
  <em>Figure 2 — Data transmission and reception during operation.</em>
</p>

<br>

<div align="center">

## 🎥 Practical demonstration

Watch the system operating and transmitting data in a short demonstration:

### [▶ Watch the video on YouTube](https://www.youtube.com/watch?v=WR5zdiHbGd0)

</div>

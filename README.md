```html
<p align="left">🇧🇷</p>

<h1 align="center">
  Arquitetura de Comunicação Óptica via Laser
</h1>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Este projeto apresenta um sistema de comunicação óptica bidirecional de médio alcance baseado em <strong>FSO — Free Space Optics</strong>, integrando hardware e software para a transmissão de dados por meio de um feixe de laser.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;O sistema utiliza dois microcontroladores <strong>Arduino Nano ESP32</strong>, configurados como pontos de acesso Wi-Fi e responsáveis por hospedar interfaces web. Por meio delas, o usuário pode enviar mensagens de texto e arquivos de imagem, além de visualizar o conteúdo recebido.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;A transmissão ocorre pela modulação digital do laser, na qual os bits <strong>0</strong> e <strong>1</strong> são representados pelos estados desligado e ligado, respectivamente. Os dados são enviados de forma sequencial, detectados por um fototransistor e reconstruídos pelo microcontrolador receptor.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;O enlace óptico alcançou <strong>37,4 metros sem perda de pacotes</strong>. Nesse limite, a atenuação e a divergência do feixe afetam diretamente a relação sinal-ruído, exigindo alinhamento óptico preciso e um protocolo resiliente para detectar, sincronizar e reconstruir os dados mesmo com o enfraquecimento do sinal.
</p>

<br>

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

<p align="center">
  <img
    src="images/sistema-fso-02.jpg"
    alt="Transmissão de dados por laser"
    width="850"
  />
</p>

<p align="center">
  <em>Figura 2 — Transmissão e recepção de dados durante a operação.</em>
</p>

<br>

<div align="center">

## 🎥 Demonstração prática

Veja o sistema operando e transmitindo dados em uma demonstração curta.

### [▶ Assistir ao vídeo no YouTube](https://www.youtube.com/watch?v=WR5zdiHbGd0)

</div>

<br>

---

<br>

<p align="left">🇬🇧</p>

<h1 align="center">
  Laser-Based Optical Communication Architecture
</h1>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;This project presents a medium-range bidirectional optical communication system based on <strong>FSO — Free-Space Optics</strong>, integrating hardware and software for data transmission through a laser beam.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;The system uses two <strong>Arduino Nano ESP32</strong> microcontrollers configured as Wi-Fi access points and web interface hosts. Through these interfaces, users can send text messages and image files while viewing the received content.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Transmission is performed through digital modulation of the laser beam, with bits <strong>0</strong> and <strong>1</strong> represented by the laser's off and on states, respectively. The data is transmitted sequentially, detected by a phototransistor, and reconstructed by the receiving microcontroller.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;The optical link reached <strong>37.4 meters with no packet loss</strong>. At this range, beam attenuation and divergence directly affect the signal-to-noise ratio, requiring precise optical alignment and a resilient protocol to detect, synchronize, and reconstruct the data even as the signal weakens.
</p>

<br>

<p align="center">
  <img
    src="images/fso-system-01.jpg"
    alt="Laser-based optical communication system"
    width="850"
  />
</p>

<p align="center">
  <em>Figure 1 — Optical communication system structure.</em>
</p>

<br>

<p align="center">
  <img
    src="images/fso-system-02.jpg"
    alt="Laser data transmission"
    width="850"
  />
</p>

<p align="center">
  <em>Figure 2 — Data transmission and reception during operation.</em>
</p>

<br>

<div align="center">

## 🎥 Practical demonstration

Watch the system operating and transmitting data in a short demonstration.

### [▶ Watch the video on YouTube](https://www.youtube.com/watch?v=WR5zdiHbGd0)

</div>
```

Para usar imagens do computador, envie-as ao repositório dentro de uma pasta `images` e mantenha os nomes usados no código, ou altere os caminhos `src`.

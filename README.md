POR  <img
    src="https://static.todamateria.com.br/upload/ba/nd/bandeira-do-brasil-og.jpg"
    alt="Bandeira do Brasil"
    width="20"
/>

# <div align="center">Comunicação Óptica via Laser </div>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Este projeto apresenta um sistema de comunicação óptica bidirecional de médio alcance baseado em <strong>FSO — Free Space Optics</strong>, integrando hardware e software para transmitir dados por meio de um feixe de laser.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;O sistema utiliza dois microcontroladores <strong>Arduino Nano ESP32</strong>, configurados como pontos de acesso Wi-Fi e responsáveis por hospedar interfaces web. Por meio delas, o usuário pode enviar mensagens de texto e arquivos de imagem, além de visualizar o conteúdo recebido.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;A transmissão ocorre pela modulação digital do laser. Os bits <strong>0</strong> e <strong>1</strong> são representados pelos estados desligado e ligado, respectivamente. Os dados são enviados sequencialmente, detectados por um fototransistor e reconstruídos pelo microcontrolador receptor.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;O enlace óptico alcançou <strong>37,4 metros sem perda de pacotes</strong>. Nesse limite, a atenuação e a divergência do feixe afetam diretamente a relação sinal-ruído, exigindo alinhamento óptico preciso e um protocolo resiliente para detectar, sincronizar e reconstruir os dados mesmo com o enfraquecimento do sinal.
</p>

---

## Imagens do projeto

<table>
  <tr>
    <td align="center" width="50%">
      <img
        src="images/base_movel_sistema_fso.png"
        alt="Estrutura do módulo móvel do sistema FSO"
        width="85%"
      />
    </td>
    <td align="center" width="50%">
      <img
        src="images/base_fixa_sistema_fso.png"
        alt="Estrutura do módulo fixo do sistema FSO"
        width="85%"
      />
    </td>
  </tr>
  <tr>
    <td align="center">
      <sub><strong>Figura 1</strong> — Estrutura do módulo MÓVEL do sistema FSO.</sub>
    </td>
    <td align="center">
      <sub><strong>Figura 2</strong> — Estrutura do módulo FIXO do sistema FSO.</sub>
    </td>
  </tr>
</table>

---

## 🎥 Demonstração prática

> Veja o sistema operando e transmitindo dados em uma demonstração curta.

### [▶ Assistir ao vídeo no YouTube](https://www.youtube.com/watch?v=WR5zdiHbGd0)

---

## Escopo deste repositório

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Este repositório contém especificamente o sistema de <strong>alinhamento automático do enlace óptico</strong> desenvolvido para o projeto FSO, incluindo o controle dos motores, a leitura dos sensores, a calibração do ambiente, a busca pelo feixe de laser e o posicionamento preciso dos módulos transmissor e receptor.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Embora o sistema físico completo tenha sido desenvolvido em colaboração por uma equipe de três integrantes — abrangendo comunicação óptica, modelagem mecânica 3D, integração de hardware e alinhamento automático —, este código-fonte é dedicado exclusivamente ao pipeline de alinhamento.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;A implementação contempla as rotinas responsáveis pela detecção do feixe, movimentação dos eixos, calibração dos sensores, correção de posicionamento e manutenção do alinhamento necessário para estabelecer o enlace óptico. Essa parte do sistema foi projetada e implementada por <strong>Marco Vendramin</strong>.
</p>

---------------------------------------------------------------------------------------------

ENG   <img
    src="https://upload.wikimedia.org/wikipedia/commons/thumb/8/83/Flag_of_the_United_Kingdom_%283-5%29.svg/330px-Flag_of_the_United_Kingdom_%283-5%29.svg.png"
    alt="Bandeira do Reino Unido"
    width="20"
  />

# <div align="center">Laser-Based Optical Communication </div>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;This project presents a medium-range bidirectional optical communication system based on <strong>FSO — Free-Space Optics</strong>, integrating hardware and software to transmit data through a laser beam.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;The system uses two <strong>Arduino Nano ESP32</strong> microcontrollers configured as Wi-Fi access points and web interface hosts. Through these interfaces, users can send text messages and image files while viewing the received content.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Transmission is performed through digital modulation of the laser beam. Bits <strong>0</strong> and <strong>1</strong> are represented by the laser's off and on states, respectively. The data is transmitted sequentially, detected by a phototransistor, and reconstructed by the receiving microcontroller.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;The optical link reached <strong>37.4 meters with no packet loss</strong>. At this range, beam attenuation and divergence directly affect the signal-to-noise ratio, requiring precise optical alignment and a resilient protocol to detect, synchronize, and reconstruct the data even as the signal weakens.
</p>

## Project Images

<table>
  <tr>
    <td align="center" width="50%">
      <img
        src="images/base_movel_sistema_fso.png"
        alt="Structure of the mobile module of the FSO system"
        width="85%"
      />
    </td>
    <td align="center" width="50%">
      <img
        src="images/base_fixa_sistema_fso.png"
        alt="Structure of the fixed module of the FSO system"
        width="85%"
      />
    </td>
  </tr>
  <tr>
    <td align="center">
      <sub><strong>Figure 1</strong> — Structure of the MOBILE module of the FSO system.</sub>
    </td>
    <td align="center">
      <sub><strong>Figure 2</strong> — Structure of the FIXED module of the FSO system.</sub>
    </td>
  </tr>
</table>

---

## Repository Scope

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;This repository specifically contains the <strong>automatic optical-link alignment system</strong> developed for the FSO project, including motor control, sensor readings, environmental calibration, laser-beam search, and precise positioning of the transmitter and receiver modules.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;Although the complete physical system was developed collaboratively by a three-member team — covering optical communication, 3D mechanical modeling, hardware integration, and automatic alignment — this source code is dedicated exclusively to the alignment pipeline.
</p>

<p align="justify">
&nbsp;&nbsp;&nbsp;&nbsp;The implementation includes the routines responsible for beam detection, axis movement, sensor calibration, positioning correction, and maintenance of the alignment required to establish the optical link. This part of the system was designed and implemented by <strong>Marco Vendramin</strong>.
</p>

## 🎥 Practical demonstration

> Watch the system operating and transmitting data in a short demonstration.

### [▶ Watch the video on YouTube](https://www.youtube.com/watch?v=WR5zdiHbGd0)

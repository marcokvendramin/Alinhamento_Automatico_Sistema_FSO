#pragma once

#include <Arduino.h>

// --- PÁGINA INICIAL (INDEX) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Nano - Painel</title>
    <style>
        body 
        { 
          font-family: sans-serif; 
          text-align: center; 
          background: #f0f0f0; 
          padding: 20px; 
        }
        .card 
        { 
          background: rgb(232, 230, 230); 
          outline: 2.5px solid blue; 
          padding: 20px; 
          border-radius: 40px; 
          box-shadow: 0 2px 5px rgba(0,0,0,0.1); 
          display: inline-block; 
          min-width: 300px; 
          max-width: 700px; 
        }
        .btn 
        { 
          display: inline-block; 
          padding: 15px 25px; 
          margin: 10px; 
          margin-top: 40px; 
          color: white; 
          text-decoration: none; 
          border-radius: 10px; 
          font-weight: bold; 
          transition: background-color 0.5s ease; 
        }  
        .send 
        { 
          background: #082cbee2; 
        } 
        .send:hover 
        { 
          background-color: #081f79; 
        }
        .zerar 
        { 
        display: inline-block;
        padding: 15px 25px; 
        margin: 10px;
        color: white;
        background: #dc3545;
        text-decoration: none;
        border-radius: 10px;
        font-weight: bold;
        transition: background-color 0.5s ease;
        border: none;
        cursor: pointer;
        }
        .zerar:hover 
        { 
        background-color: #c82333; 
        }
        .zerar:disabled
        {
        background: #999;
        cursor: not-allowed;
        opacity: 0.6;
        }
        .receive 
        { 
          background: #082cbee2;
        } 
        .receive:hover 
        { 
          background-color: #081f79; 
        }
        .align 
        { 
          display: block; 
          padding: 15px 25px; 
          max-width: 300px; 
          margin: 20px auto 0px auto; 
          color: white; 
          background: #545b61e4; 
        }
        .align:hover 
        { 
          background-color: #495057; 
        } 
        .status 
        { 
          color: #161616; 
          margin-top: 15px; 
        }
        .calibrar 
        { 
          display: block; 
          padding: 15px 25px; 
          max-width: 300px; 
          margin: 10px auto 0px auto; 
          color: white; 
          background: #1a7a3c; 
        }
        .calibrar:hover 
        { 
          background-color: #145c2d; 
        }
        .status 
        { 
          color: #161616; 
          margin-top: 15px; 
        }
        .status-zerar
        {
          margin-top: 10px;
          font-size: 0.9em;
          color: #666;
          min-height: 20px;
        }
        .titulo-botoes
        {
          margin-top: 30px;
          margin-bottom: 15px;
          font-weight: bold;
          color: #333;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>Escolha o modo de operação</h1>
        <div class="status">
            Status Atual: <br>
            <strong id="txt-status" style="color: #007BFF; font-size: 1.2em;">%MODO_ATUAL%</strong>
        </div>
        <a href="/set-envio" class="btn send">Envio de dados</a>
        <a href="/set-recepcao" class="btn receive">Recepção de dados</a>
        <a href="/set-alinhamento" class="btn align">Alinhar as bases</a>
        <a href="/set-calibracao" class="btn calibrar">Calibrar Ambiente</a>
        <br>
        <button class="btn zerar" id="btn-zerar" onclick="executarZerar()">
            Encerrar canal de comunicação
        </button>
        <div class="status-zerar" id="status-zerar"></div>
    </div>

    <script>
        // Atualiza o texto do status a cada 1 segundo (Real-time)
        setInterval(function() {
            fetch('/status-atual')
                .then(response => response.text())
                .then(data => {
                    document.getElementById('txt-status').innerText = data;
                });
        }, 1000);

        // Função para executar o retorno ao origen
        async function executarZerar() {
            const btn = document.getElementById('btn-zerar');
            const statusDiv = document.getElementById('status-zerar');
            
            // Desabilita o botão durante o processo
            btn.disabled = true;
            statusDiv.innerText = "Processando... Retornando à origem...";
            statusDiv.style.color = "#FF8C00";
 
            try {
                const response = await fetch('/zerar');
                const mensagem = await response.text();
                
                if (response.ok) {
                    statusDiv.innerText = mensagem;
                    statusDiv.style.color = "green";
                } else {
                    statusDiv.innerText = "Erro: " + mensagem;
                    statusDiv.style.color = "red";
                }
            } catch (error) {
                statusDiv.innerText = "Erro de conexão ao tentar retornar";
                statusDiv.style.color = "red";
                console.error("Erro:", error);
            }
 
            // Reabilita o botão após 3 segundos
            setTimeout(() => {
                btn.disabled = false;
                statusDiv.innerText = "";
            }, 3000);
        }

    </script>
</body>
</html>
)rawliteral";

// --- PÁGINA DE RECEPÇÃO ---
const char recepcao_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<html lang="pt-br">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Recepção de dados</title>
  <style>
    body 
    { 
      font-family: sans-serif; 
      margin: 20px; 
      text-align: center; 
      background: #f0f0f0; 
    }
    .card
    {
        background: rgb(232, 230, 230); 
        outline: 2.5px solid blue; 
        padding: 20px; 
        border-radius: 40px; 
        box-shadow: 0 2px 5px rgba(0,0,0,0.1); 
        display: inline-block; 
        min-width: 300px;
        max-width: 700px; 
    }
    .caixa 
    { 
      border: 2px solid #333; 
      padding: 15px; 
      background: #fff; 
      white-space: pre-wrap; 
      height: 350px; 
      overflow-y: auto; 
      text-align: left; 
      border-radius: 10px; 
      font-family: monospace; 
    }
    #status-val 
    { 
      font-weight: bold; 
      color: blue; 
    }
    .btn-voltar 
    { 
      display: inline-block; 
      margin-top: 20px; 
      padding: 12px 25px; 
      background: #6c757d; 
      color: white; 
      text-decoration: none; 
      border-radius: 8px; 
      font-weight: bold; 
    }
    .btn-voltar:hover 
    { 
      background: #495057; 
    }
    .btn-ok 
    { 
      display: inline-block; 
      margin-top: 20px; 
      padding: 12px 25px; 
      background: #6c757d; 
      color: white; 
      text-decoration: none; 
      border-radius: 8px; 
      font-weight: bold; 
    }
    .btn-ok:not(:disabled):hover 
    { 
      background: #495057; 
    }
  </style>
</head>
<body>
    <div class="card">
        <h1>Modo Recepção de Dados</h1>
        <p>Status: <span id="status-val">Aguardando dados...</span></p>
        <div class="caixa" id="alvo-da-lista"></div>
        <div id="container-imagem" style="margin-top: 15px; text-align: center;"></div>
        <br>
        <a href="/voltar" class="btn-voltar">Voltar</a>
        <br>
        <a href="/set-recepcao" id="btn-ok" class="btn-ok">Limpar Caixa</a>
    </div>

  <script>
    let contador = 0;

    async function solicitarDados() {
        const caixa = document.getElementById('alvo-da-lista');
        const status = document.getElementById('status-val');
        const btn = document.getElementById('btn-ok');
        const containerImg = document.getElementById('container-imagem');
        
        let ativo = true;
        let imageDetect = false;
        let contadorCache = 0;
        let bytesRecebidos = [];

        await new Promise(r => setTimeout(r, 500));

        while(ativo) {
            btn.style.pointerEvents = "none";
            btn.style.opacity = "0.7";
            
            try {
                const res = await fetch(`/proximo-char?nocache=${contadorCache++}`);
                const dadosBrutos = await res.text();
                
                if (dadosBrutos === "IDLE")
                {
                    await new Promise(r => setTimeout(r, 200));
                }
                else if (dadosBrutos === "EOF")
                {
                    status.innerText = "Concluído!";
                    status.style.color = "green";
                    btn.style.pointerEvents = "auto";
                    btn.style.opacity = "1.1";
                    ativo = false;

                    if (bytesRecebidos.length > 0) 
                    {
                        const primeiroByte = bytesRecebidos[0];
                        if (primeiroByte === 255 || primeiroByte === 137) 
                        {
                            mostrarImagemNaTela(bytesRecebidos, containerImg);
                        }
                        else 
                        {
                            containerImg.style.display = 'none';
                            containerImg.innerHTML = "";
                        }
                    }
                }
                else if (dadosBrutos.length === 2) 
                {
                    const primeiroByteHex = dadosBrutos;
                    
                    if (primeiroByteHex === "89" || primeiroByteHex === "FF") 
                    {
                        imageDetect = true;
                        caixa.style.display="none";
                        let formato = 'image/jpeg';
                        if (primeiroByteHex === "89") formato = 'image/png';
                        containerImg.innerHTML = `
                            <h3 style="margin-top: 10px;">Imagem Detectada (${formato.split('/')[1].toUpperCase()}):</h3>
                        `;
                        status.innerText = "Processando imagem...";
                        bytesRecebidos.push(parseInt(dadosBrutos, 16));
                    }
                    else
                    {
                        caixa.innerText += String.fromCharCode(parseInt(dadosBrutos, 16));
                        caixa.scrollTop = caixa.scrollHeight;
                        status.innerText = "Processando texto...";
                    }

                }
                else if (dadosBrutos.length > 2) 
                {
                    // Pega os 2 primeiros caracteres, tamanho
                    const tamanhoHex = dadosBrutos.substring(0, 2);
                    const tamanhoReal = parseInt(tamanhoHex, 16); // Converte de HEX para nº

                    // Separa oq é dado
                    const limiteCaracteres = tamanhoReal * 2;
                    const dadosHexPuros = dadosBrutos.substring(2, 2 + limiteCaracteres);

                    // Traduz os dados Hex para string legível
                    let textoDesteBloco = "";
                    for (let i = 0; i < limiteCaracteres; i += 2) {
                        const parHex = dadosHexPuros.substring(i, i + 2);

                        if (imageDetect)
                        {
                            bytesRecebidos.push(parseInt(parHex, 16));
                        }
                        else
                        {
                            const codigoAscii = parseInt(parHex, 16);
                            const caractere = String.fromCharCode(codigoAscii);

                            // Imprime apenas os caracteres da ASCII, para evitar lixo visual
                            if (!isNaN(codigoAscii) && (codigoAscii >= 32 || codigoAscii === 10 || codigoAscii === 13 || codigoAscii > 127)) 
                            {
                                caixa.innerText += caractere;
                                caixa.scrollTop = caixa.scrollHeight;
                            }
                        }
                    }
                }
            } 
            catch (e) 
            {
                console.error("Erro na leitura do laser:", e);
                status.innerText = "Erro na conexão!";
                status.style.color = "red";
                btn.style.pointerEvents = "auto";
                btn.style.opacity = "1.1";
                ativo = false;
            }
        }
    }

    function mostrarImagemNaTela(bytes, container) 
    {
        const arrayBuffer = new Uint8Array(bytes);
        let formato = 'image/jpeg';
        if (arrayBuffer[0] === 137) formato = 'image/png';

        const blob = new Blob([arrayBuffer], { type: formato });
        const urlDaImagem = URL.createObjectURL(blob);
        
        container.innerHTML = `
            <h3 style="margin-top: 10px;">Imagem Detectada (${formato.split('/')[1].toUpperCase()}):</h3>
            <img src="${urlDaImagem}" alt="Foto via Laser" style="max-width: 100%; max-height: 250px; border: 2px solid #333; border-radius: 5px;">
        `;
    }

    window.onload = solicitarDados;
  </script>
</body>
</html>
)rawliteral";

// --- PÁGINA DE ENVIO ---
const char envio_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Envio de dados</title>
    <style>
        body
        {
            font-family: sans-serif; 
            margin: 20px; 
            text-align: center; 
            background: #f0f0f0;
        }
        .card
        {
            background: rgb(232, 230, 230); 
            outline: 2.5px solid blue; 
            padding: 20px; 
            border-radius: 40px; 
            box-shadow: 0 2px 5px rgba(0,0,0,0.1); 
            display: inline-block; 
            min-width: 300px;
            max-width: 700px;
        }
        .input
        {
            border-radius: 5px;
            height: 200px;
            width: 90%;
            max-width: 90%;
            box-sizing: border-box;
            resize: none;
        }
        .fake-input
        {
            display:none;
        }
        .img-input
        { 
            display: inline-block; 
            margin-top: 20px; 
            padding: 12px 25px; 
            background: #6c757d; 
            color: white; 
            text-decoration: none; 
            border-radius: 8px; 
            font-weight: bold; 
            font-size: small;
        }
        .img-input:hover 
        { 
            background: #495057; 
        }
        .btn-enviar
        {
            display: inline-block; 
            margin-top: 20px; 
            padding: 12px 25px; 
            background: #6c757d; 
            color: white; 
            text-decoration: none; 
            border-radius: 8px; 
            width: 220px;
            font-weight: bold;
            font-size: large; 
        }
        .btn-enviar:not(:disabled):hover 
        { 
            background: #495057; 
        }
        .btn-voltar
        { 
            display: inline-block; 
            margin-top: 20px; 
            padding: 12px 25px; 
            background: #6c757d; 
            color: white; 
            text-decoration: none; 
            border-radius: 8px; 
            font-weight: bold; 
            font-size: small;
        }
        .btn-voltar:hover 
        { 
            background: #495057; 
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>Modo Envio de Dados</h1>
        <textarea class="input" id="mensagem" placeholder="Digite aqui a mensagem a ser enviada..."></textarea>
        <input type="file" id="img-input" class="fake-input" accept=".jpg, .jpeg, .png" onchange="imgUpload()">
        <label for="img-input" class="img-input">
            Selecione uma foto aqui
        </label>
        <br>
        <button class="btn-enviar" id="btn-submit" onclick="enviarDados()">Enviar</button> 
        <div id="status-envio"></div>
        <a href="/voltar" class="btn-voltar">Voltar</a>
    </div>

    <script>
        let contador = 0;

        function lerArquivoComoBytes(arquivo) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onload = (e) => resolve(new Uint8Array(e.target.result));
            reader.onerror = (e) => reject(e);
            reader.readAsArrayBuffer(arquivo);
        });
        }

        function imgUpload()
        {
            const img = document.getElementById('img-input');
            const caixa = document.getElementById('mensagem');

            const labelImg = document.querySelector('label[for="img-input"]');

            if (img && img.files && img.files.length > 0)
            {
                caixa.style.display="none";
                labelImg.innerText = "Upload realizado com sucesso!";
                labelImg.style.color = "white";
                labelImg.style.backgroundColor = "green";
                labelImg.style.pointerEvents = "none";
            }
        }

        async function enviarDados() {
            const caixa = document.getElementById('mensagem');
            const btn = document.getElementById('btn-submit');
            const status = document.getElementById('status-envio');
            const img = document.getElementById('img-input');
            const texto = caixa.value;

            const labelImg = document.querySelector('label[for="img-input"]');

            const temArquivo = img && img.files && img.files.length > 0;

            if (texto.length === 0 && !temArquivo) return;

            // Bloqueia interface durante o envio
            btn.disabled = true;
            labelImg.style.pointerEvents = "none";
            caixa.disabled = true;
            status.innerText = "Iniciando processo...";
            
            let envioFinalizado = true;
            const TAMANHO_CHUNK = 512; 
            let ponteiro = 0;

            if (temArquivo)
            {
                const arquivo = img.files[0];

                // Limita o tamanho em 2KB
                if (arquivo.size > 2 * 1024) {
                    alert("Imagem muito grande! Máximo 2KB.");
                    btn.disabled = false;
                    caixa.disabled = false;
                    status.innerText = "";
                    return;
                }

                const arrayDeBytes = await lerArquivoComoBytes(arquivo);

                while (ponteiro < arrayDeBytes.length) 
                {
                    // Corta um pedaço da imagem q vai ser enviado
                    const pedaco = arrayDeBytes.subarray(ponteiro, ponteiro + TAMANHO_CHUNK);
                    const tamanhoRealDoBloco = pedaco.length;

                    // Converte o tamanho do bloco numa string de 4 dígitos
                    const cabecalhoTexto = String(tamanhoRealDoBloco).padStart(4, '0');

                    // Converte os bytes do pedaço da imagem para um HEX, q nn se perde em http
                    let dadosDoBlocoHex = "";
                    for (let i = 0; i < pedaco.length; i++) {
                        dadosDoBlocoHex += pedaco[i].toString(16).padStart(2, '0').toUpperCase();
                    }

                    // Junta cabeçalho de texto + dados
                    const mensagemFinal = cabecalhoTexto + dadosDoBlocoHex;

                    status.innerText = `Processando imagem: ${Math.round(((ponteiro + tamanhoRealDoBloco) / arrayDeBytes.length) * 100)}%`;

                    try 
                    {
                        // Envia tudo como String no corpo da requisição
                        await fetch(`/receber-bloco?cnt=${contador++}`, {
                            method: 'POST',
                            headers: { 'Content-Type': 'text/plain' }, // Avisa que é texto simples
                            body: mensagemFinal 
                        });
                    }
                    catch(e) {
                        console.error("Erro na rede ao enviar o bloco");
                        status.innerText = "Erro na conexão!";
                        envioFinalizado = false;
                        break;
                    }

                    ponteiro += tamanhoRealDoBloco;
                }
            }
            else
            {
                while (ponteiro < texto.length) 
                {
                    // A lógica aqui é +- o mesmo da imagem
                    const pedaco = texto.substring(ponteiro, ponteiro + TAMANHO_CHUNK);
                    const tamanhoRealDoBloco = pedaco.length;

                    const cabecalhoTexto = String(tamanhoRealDoBloco).padStart(4, '0');

                    let dadosDoBlocoHex = "";
                    for (let i = 0; i < pedaco.length; i++) {
                        dadosDoBlocoHex += pedaco.charCodeAt(i).toString(16).padStart(2, '0').toUpperCase();
                    }                    

                    const mensagemFinal = cabecalhoTexto + dadosDoBlocoHex;

                    status.innerText = `Processando texto: ${Math.round(((ponteiro + tamanhoRealDoBloco) / texto.length) * 100)}%`;

                    try {
                        // Envia tudo como String no corpo da requisição
                        await fetch(`/receber-bloco?cnt=${contador++}`, {
                            method: 'POST',
                            headers: { 'Content-Type': 'text/plain' }, // Avisa que é texto simples
                            body: mensagemFinal 
                        });
                    }
                    catch(e) {
                        console.error("Erro na rede ao enviar o bloco");
                        status.innerText = "Erro na conexão!";
                        envioFinalizado=false;
                        break;
                    }

                    ponteiro += tamanhoRealDoBloco;

                }
            }

            if(envioFinalizado)
            {
                // Sinaliza fim do envio
                await fetch(`/receber-char?c=EOF&cnt=${contador++}`);
                
                status.innerText = "Envio sendo realizado!";
                status.style.color = "green";
            }
        }
    </script>
</body>
</html>
)rawliteral";

// --- PÁGINA PLACEHOLDER (ALINHAMENTO) ---
const char placeholder_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="pt-br">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Alinhamento</title>
    <style>
        body
        {
            font-family: sans-serif; 
            margin: 20px; 
            text-align: center; 
            background: #f0f0f0;
        }
        .card
        {
            background: rgb(232, 230, 230); 
            outline: 2.5px solid blue; 
            padding: 20px; 
            border-radius: 40px; 
            box-shadow: 0 2px 5px rgba(0,0,0,0.1); 
            display: inline-block; 
            min-width: 300px; 
            max-width: 450px; 
        }
        .title
        {
            color: #082cbe;
            font: bold;
        }
        .txt-status
        {
            color: #d71414; 
            font-size: 1.2em;
        }
        .btn-voltar
        { 
            display: inline-block; 
            margin-top: 20px; 
            padding: 12px 25px; 
            background: #6c757d; 
            color: white; 
            text-decoration: none; 
            border-radius: 8px; 
            font-weight: bold;
        }
        .btn-voltar:hover 
        { 
            background: #495057; 
        }
    </style>
</head>
<body>
    <div class="card">
        <h1 class="title">Alinhamento</h1>
        <h3>Status atual:</h3>
        <span id="status-val" class="txt-status"> %MODO_ATUAL% </span>
        <br>
        <a href="/voltar-alinhamento" class="btn-voltar">Voltar</a>
        <br>
    </div>

</body>
</html>
)rawliteral";

// --- PÁGINA PLACEHOLDER (CALIBRAÇÃO) ---
const char calibracao_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="pt-br">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Calibração</title>
    <style>
        body
        {
            font-family: sans-serif; 
            margin: 20px; 
            text-align: center; 
            background: #f0f0f0;
        }
        .card
        {
            background: rgb(232, 230, 230); 
            outline: 2.5px solid blue; 
            padding: 20px; 
            border-radius: 40px; 
            box-shadow: 0 2px 5px rgba(0,0,0,0.1); 
            display: inline-block; 
            min-width: 300px; 
            max-width: 450px; 
        }
        .title
        {
            color: #1a7a3c;
            font: bold;
        }
        .txt-status
        {
            color: #d71414; 
            font-size: 1.2em;
        }
        .btn-voltar
        { 
            display: inline-block; 
            margin-top: 20px; 
            padding: 12px 25px; 
            background: #6c757d; 
            color: white; 
            text-decoration: none; 
            border-radius: 8px; 
            font-weight: bold;
        }
        .btn-voltar:hover 
        { 
            background: #495057; 
        }
    </style>
</head>
<body>
    <div class="card">
        <h1 class="title">Calibração de Ambiente</h1>
        <h3>Status atual:</h3>
        <span id="status-val" class="txt-status"> %MODO_ATUAL% </span>
        <br>
        <a href="/voltar-calibracao" class="btn-voltar">Voltar</a>
        <br>
    </div>

    <script>
    // Atualiza o texto do status a cada 1 segundo
    setInterval(function() 
    {
        const status = document.getElementById('status-val');
        
        fetch('/status-atual')
            .then(response => response.text())
            .then(data => {
                status.innerText = data;

                if (status.innerText !== "Concluído")   // ← string errada, nunca batia
                {
                    status.disabled = true;
                }
                else
                {
                    status.style.color = "green";
                    status.disabled = false;
                }
            });
    }, 1000);

    // DEPOIS
    setInterval(function() 
    {
        const status = document.getElementById('status-val');
        
        fetch('/status-atual')
            .then(response => response.text())
            .then(data => {
                status.innerText = data;

                if (data === "CALIBRAÇÃO CONCLUÍDA")
                {
                    status.style.color = "green";
                }
                else
                {
                    status.style.color = "#d71414";
                }
            });
    }, 1000);
    </script>
</body>
</html>
)rawliteral";

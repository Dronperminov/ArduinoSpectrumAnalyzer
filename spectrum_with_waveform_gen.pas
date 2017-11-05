{$mainresource spectrum.res}
{$apptype windows}
{$reference 'System.Windows.Forms.dll'}

//program spectrum_analyzer;

uses graphAbc, system.IO.Ports, system, system.IO, System.Windows.Forms, System.Drawing;

const
  port_name = 'COM3'; {имя последовательного порта}
  port_speed = 115200; {скорость последовательного порта}
  
  screen_width = 800; {ширина графического окна}
  screen_height = 300; {высота графического окна}
  
  margin_x = 20;
  margin_y = 50;
  
  zero_x = margin_x;
  
  bar_margin_x = 2;
  bar_margin_y = 1;
  
  top = 766 - screen_height - 80;
  left = 1366 - screen_width - 150;
  
  font_color = rgb(0, 0, 0); // цвет фона
  text_color = clWhite; // цвет текста
  
  spectrum_bar_color = rgb(0, 0, 255); // цвет обычных полос спектра
  spectrum_peak_color = rgb(0, 255, 255); // цвет полосы с максмальной амплитудой
  
  line_color = clWhite; // цвет линий
  
  max_data = 4096; {максимальное кол-во элементов в массиве данных }

var  
  zero_y := screen_height - margin_y; {начальная координа по у}

  length_x := screen_width - 2 * margin_x; {длина оси х}
  length_y := screen_height - margin_y; {длина оси у}

type  
    measurement_info = record
    max_level:word;
    peak_index:word;
    log_base:word;
    sample_freq:word;
    total_band_count:word;
    data_size:word;
    bar_width:integer;
    peak_bar_index:integer;
    peak_freq:real;
  end;
  
  TLabel = System.Windows.Forms.Label;

  input_point = record 
    level:word;
    band_count:word;
  end;
  
  output_point = record
    level:real;
    frequency:real;
  end;

  input_array = array[1..max_data] of input_point;
  output_array = array[1..max_data] of output_point;

var
  port: SerialPort; {СОМ порт}

  input_data:input_array;
  output_data:output_array;
  measurement:measurement_info;
  
  generatorOnCheckBox:CheckBox;
  controlPanel: TableLayoutPanel;
  frequencyLabel:TLabel;
  waveformLabel:TLabel;
  frequencyTextBox:TextBox;
  waveformComboBox:ComboBox;

{Рисование координатных осей}
procedure DrawAxis(measurement:measurement_info; var dst_data:output_array);
var
  i:integer;
  text_height:integer;
  x, y:integer;
  lbl:string;
  lbl_ratio:integer;
  
begin  
   SetBrushColor(text_color);
   SetFontColor(text_color);

  text_height := TextHeight('0'); 
    
  zero_y := screen_height - margin_y;

  length_x := screen_width - 4 * margin_x;
  length_y := screen_height - round(5 / 3 * margin_y);

  line(zero_x, zero_y, zero_x + length_x, zero_y, line_color); 
  line(zero_x, zero_y, zero_x, zero_y - length_y, line_color);
  
  SetWindowCaption('Анализатор спектра сигнала (dark theme)');
  
  SetBrushColor(FONT_COLOR);
  
  TextOut(zero_x + length_x + 10, zero_y - text_height div 2, 'f (Hz)');
  TextOut(zero_x - TextWidth('Amp') div 2, zero_y - length_y - text_height - 5, 'Amp');
  
  if measurement.peak_index <> 0 then
    TextOut(zero_x + length_x div 2, zero_y - length_y - text_height - 10, 'Пик: ' + IntToStr(round(measurement.peak_freq)) + ' Hz');
    
  TextOut(zero_x + length_x div 2 + 5 * margin_x, zero_y - length_y - text_height - 10, 'N полос: ' + IntToStr(measurement.data_size));
  
  //TextOut(zero_x + length_x div 2 - 5 * margin_x, zero_y - length_y - text_height - 10, 'Freq: ' + IntToStr(freq));
  
  lbl_ratio := text_height div measurement.bar_width + 1;
  
  for i := 1 to measurement.data_size do
  begin
    x := zero_x + bar_margin_x + (i - 1) * measurement.bar_width + measurement.bar_width div 2;
    y := zero_y;
      
    Coordinate.SetTransform(x, y, 90, 1, 1);  
    line(0, 0, 5, 0, line_color);
    
    if (i - 1) mod lbl_ratio = 0 then
    begin    
     if dst_data[i].frequency >= 1000 then
        lbl := FloatToStr(round(trunc(dst_data[i].frequency / 100)) / 10) + 'k'
      else
         lbl := IntToStr(round(dst_data[i].frequency));
      
      TextOut(8, -(text_height div 2), lbl);         
    end;
  end;
         
  Coordinate.SetTransform(0, 0, 0, 1, 1);
end;

{Рисование одной полосы спектра}
procedure DrawSpectrumBar (index, width:integer; point:output_point; c:color);
begin
  SetBrushColor(c);
  FillRect(zero_x + bar_margin_x + (index - 1) * width, zero_y - bar_margin_y, zero_x + bar_margin_x + width * index - 1, zero_y - bar_margin_y - round(length_y * point.level));
end;

{Процедура рисования полос спектра}
procedure DrawSpectrum (measurement:measurement_info; var data:output_array);
var 
  i:integer;
  c:color;
begin 
  SetPenWidth(0);
  
  for i := 1 to measurement.data_size do
  begin
    if i = measurement.peak_bar_index then
      c := spectrum_peak_color
    else
      c := spectrum_bar_color;
      
    DrawSpectrumBar(i, measurement.bar_width, data[i], c);
  end;
end;

{функция считывания слова из файла}
function ReadWord (var port: SerialPort):word;
var high, low:byte;
begin
   Try
     high := port.ReadByte;  
     low := port.ReadByte;   
   
   except 
       on System.IO.IOException do
         CloseWindow;
  end; 
   
   ReadWord := (high shl 8) or low;
end;

{Процедура чтения байтов до маркера начала пакета (0хffff)}
procedure ReadPacketMarker (var port: SerialPort);
begin
  Window.Clear(font_color); 
  
  Try
    repeat
     repeat
     until port.ReadByte = $ff;
  until port.ReadByte = $ff;   
    
  except 
       on System.IO.IOException do
         CloseWindow;
  end;  
end;

{Процедура чтения пакета данных с СОМ порта}
function ReadPacket (var port: SerialPort; var measurement:measurement_info; var data:input_array):boolean;
var 
  i:integer;
  end_marker:word;
begin
     port.DiscardInBuffer;
   
     ReadPacketMarker(port);
     
     measurement.max_level := ReadWord(port); // макисмальный уровень сигнала
     measurement.peak_index := ReadWord(port); // индекс элемента с максимальной амплитудой
     measurement.log_base := ReadWord(port); // значение логарифма
     measurement.sample_freq := ReadWord(port); // частота каждой выборки
     measurement.total_band_count := ReadWord(port); // DATA_SIZE
     
     measurement.data_size := ReadWord(port);
     
     if measurement.data_size > max_data then
       measurement.data_size := max_data;  
   
     for i := 1 to measurement.data_size do
     begin
       data[i].band_count := ReadWord(port);
       data[i].level := ReadWord(port);
     end;
       
     end_marker := ReadWord(port);    
     
     ReadPacket := end_marker = $0000;
end;

{Процедура преобразования сигнала с фиксированной точкой в сигнал с плавующей точкой}
procedure PrepareData(var measurement:measurement_info; var src_data:input_array; var dst_data:output_array);
var 
  i: integer;
  bar_index, next_bar_index: integer;
  band_freq: real;
begin
  {TODO обработать случай, когда элементов массива больше, чем доступных точек}
  measurement.bar_width := length_x div measurement.data_size;
  
  band_freq := measurement.sample_freq / measurement.total_band_count;
  
  measurement.peak_freq := measurement.peak_index * band_freq;
  
  bar_index := 1;
  
  for i := 1 to measurement.data_size do
  begin
    dst_data[i].level := src_data[i].level / measurement.max_level;
    
    if dst_data[i].level > 1.0 then
      dst_data[i].level := 1.0;
    
    next_bar_index := bar_index + src_data[i].band_count;
     
    dst_data[i].frequency := band_freq * ((bar_index + next_bar_index - 1) div 2);
    
    if (measurement.peak_index >= bar_index) and (measurement.peak_index < next_bar_index) then
      measurement.peak_bar_index := i;
    
    bar_index := next_bar_index;
  end;
end;

function SetupPort(var port: SerialPort):boolean;
begin
   Try
    port := SerialPort.Create(port_name, port_speed, Parity.None, 8, StopBits.One);  
    port.Open;  
    SetupPort := true;
    
    except 
       on System.IO.IOException do begin
         SetupPort := false;
         SetWindowSize(330, 60);
         CenterWindow;
         ClearWindow(font_color);
         SetBrushColor(font_color);
         SetFontColor(text_color);
         SetFontSize(25);
         TextOut(10, 5, 'Serial port can''t open!');
         sleep(5000);
         CloseWindow;
         end;
   end;
end;

procedure SendGenerator(var port:Serialport; waveform:string; freq:integer);
begin
   port.Write(chr(10) + waveform + ' ' + IntToStr(freq) + chr(10));
end;

procedure StopGenerator;
begin
   generatorOnCheckBox.Checked := false;
   SendGenerator(port, 'stop', 0);
end;

procedure RunGenerator;
var
   freqStr:string;
   freq:integer;
   waveform:string;
   
begin
   if not generatorOnCheckBox.Checked then
     exit;
     
   freqStr := frequencyTextBox.Text;
   
   if freqStr = '' then begin
     stopGenerator;
     MessageBox.Show('Пожалуйста, укажите частоту', 'Ошибка', MessageBoxButtons.OK, MessageBoxIcon.Warning);
     frequencyTextBox.Focus;
     exit;
   end;
   
   try
     freq := StrToInt(freqStr);
   except 
      on System.FormatException do begin      
        stopGenerator;
        MessageBox.Show('Частота должна быть целым числом', 'Ошибка', MessageBoxButtons.OK, MessageBoxIcon.Warning);
        frequencyTextBox.Focus;
        exit;
    end;
  end;
   
   if (freq < 5) or (freq > 50000) then begin
     stopGenerator;
     MessageBox.Show('Частота должна быть от 5 Гц до 50 кГц', 'Ошибка', MessageBoxButtons.OK, MessageBoxIcon.Warning);
     frequencyTextBox.Focus;
     exit;
   end;
   
   waveform := waveformComboBox.SelectedItem.ToString;
   
   SendGenerator(port, waveform, freq);
end;



{Процедура инициализации}
function Setup (var port: SerialPort):boolean;
begin
  SetWindowSize(screen_width, screen_height);
  SetWindowIsFixedSize(true);
  SetGraphABCIO;
  
  SetWindowLeft(left);
  SetWindowTop(top);
  Setup := SetupPort(port);
  
  LockDrawing;
end;

procedure generatorOnChanged(o:Object; e: EventArgs);
begin
   if generatorOnCheckBox.Checked then
     RunGenerator
   else
     StopGenerator;
end;

procedure generatorParameterChanged(o:Object; e: EventArgs);
begin
   RunGenerator;
end;

procedure InitControls;
begin
   MainForm.SuspendLayout();  
   
   controlPanel := new TableLayoutPanel();
   
   controlPanel.SuspendLayout();
   
   controlPanel.ColumnCount := 1;
   controlPanel.RowCount := 5;
   
   generatorOnCheckBox := new CheckBox();
   generatorOnCheckBox.Text := 'Generator';
   generatorOnCheckBox.Location := new Point(0, 0);
   generatorOnCheckBox.Click += GeneratorOnChanged;   
   controlPanel.Controls.Add(generatorOnCheckBox);   
   
   frequencyLabel := new TLabel();
   frequencyLabel.Text := 'Frequency:';
   frequencyLabel.ForeColor := System.Drawing.Color.Black;
   frequencyLabel.Location := new Point(0, 30);
   frequencyLabel.AutoSize := true;
   controlPanel.Controls.Add(frequencyLabel);
   
   frequencyTextBox := new TextBox();
   frequencyTextBox.Multiline := false;
   frequencyTextBox.Size := new Size(70, 25);
   frequencyTextBox.Location := new Point(0, 55);
   frequencyTextBox.Text := '1000';
   frequencyTextBox.TextChanged += generatorParameterChanged;
   controlPanel.Controls.Add(frequencyTextBox);
   
   waveformLabel := new TLabel();
   waveformLabel.Text := 'Waveform:';
   waveformLabel.ForeColor := System.Drawing.Color.Black;
   waveformLabel.Location := new Point(0, 85);
   waveformLabel.AutoSize := true;
   controlPanel.Controls.Add(waveformLabel);
   
   waveformComboBox := new ComboBox();
   waveformComboBox.Location := new Point(0, 110);
   waveformComboBox.Size := new Size(70, 35);
   waveformComboBox.DropDownStyle := ComboBoxStyle.DropDownList;
   waveformComboBox.Items.Add('sin');
   waveformComboBox.Items.Add('square');
   waveformComboBox.Items.Add('saw');
   waveformComboBox.Items.Add('triangle');
   waveformComboBox.SelectedIndex := 0;
   waveformComboBox.SelectedIndexChanged += generatorParameterChanged;
   
   controlPanel.Controls.Add(waveformComboBox);
   
   controlPanel.Anchor := ((System.Windows.Forms.AnchorStyles)((integer(System.Windows.Forms.AnchorStyles.Top) or integer(System.Windows.Forms.AnchorStyles.Right))));

   controlPanel.Location := new Point(screen_width + 10, 10);
   controlPanel.Size := new Size(100, 150);
   MainForm.Controls.Add(controlPanel);   
   
   MainForm.MinimumSize := new Size(screen_width + 100, screen_height);
   
   graphABCControl.Size := new Size(screen_width, screen_height);
   
   MainForm.Show;
end;

procedure KeyDown(Key: integer);
begin
  case Key of
     VK_Left: Window.Left := Window.Left - 5;
     VK_Right: Window.Left := Window.Left + 5;
     VK_Up: Window.Top := Window.Top - 5;
     VK_Down: Window.Top := Window.Top + 5;
  end;
end;

procedure MouseDown(x,y,mb: integer);
begin
  TextOut(length_x div 2, length_y div 2, 'x: ' + IntToStr(x) + '; y: ' + IntToStr(y));
    
end;
  
begin
  MainForm.Invoke(InitControls);

  if Setup(port) then begin
    SendGenerator(port, 'stop', 0);
  
    while true do begin    
      if not ReadPacket(port, measurement, input_data) then 
        continue;
       
      PrepareData(measurement, input_data, output_data);
    
      DrawAxis(measurement, output_data);   
    
      DrawSpectrum(measurement, output_data);    
      
      Redraw; 
    end;
    
    port.Close;
  end;
end.
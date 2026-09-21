import React, { useEffect, useState, useRef } from 'react';
import { CustomTextField, CustomButton } from './CustomComponents';
import { serverGet } from '../helpers/cgihelper';
/* MUI */
import { useTheme } from '@mui/material/styles';
import ArticleIcon from '@mui/icons-material/Article';
import ClearIcon from '@mui/icons-material/Clear';

const L_CGI = '/axis-cgi/admin/systemlog.cgi?appname=acap_quake2';

const LogBox: React.FC = () => {
  /* Local states */
  const [serverLog, setServerLog] = useState<string>('');
  const [loadingLogs, setLoadingLogs] = useState<boolean>(false);

  const textFieldRef = useRef<HTMLDivElement | null>(null);

  const theme = useTheme();

  /* Scroll to the bottom when serverLog changes */
  const scrollToBottom = () => {
    if (textFieldRef.current) {
      textFieldRef.current.scrollTop = textFieldRef.current.scrollHeight;
    }
  };

  useEffect(() => {
    scrollToBottom();
  }, [serverLog]);

  /* Get app system logs */
  const getLogs = () => {
    const setData = async () => {
      setLoadingLogs(true);
      try {
        const resp = await serverGet(L_CGI);
        if ('error' in resp) {
          console.error(resp.error);
          setLoadingLogs(false);
          return;
        }
        const responseText = await resp.text();
        setServerLog(responseText);
        setLoadingLogs(false);
      } catch (error) {
        console.error('Request failed:', error);
      }
      setLoadingLogs(false);
    };
    setData();
  };

  const clearLogs = () => {
    setServerLog('');
  };

  return (
    <div>
      {/* App log box */}
      <CustomTextField
        label="Logs"
        value={serverLog}
        textFieldRef={textFieldRef}
        inputProps={{ spellCheck: false, wrap: 'off' }}
        sx={{
          '& .MuiOutlinedInput-root': {
            backgroundColor:
              theme.palette.mode === 'dark' ? '#151719' : '#f6f8fa'
          },
          '& .MuiInputBase-input': {
            fontFamily:
              'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace',
            fontSize: '13px',
            lineHeight: 1.6,
            tabSize: 4
          }
        }}
      />
      <div style={{ marginTop: '1em' }} />
      {/* App log controls */}
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between'
        }}
      >
        <CustomButton
          onClick={getLogs}
          variant="contained"
          type="button"
          disabled={loadingLogs}
          sx={{
            fontFamily: 'inherit',
            backgroundColor: 'primary.main',
            color: 'primary.contrastText',
            '&:hover': {
              backgroundColor: 'primary.dark'
            }
          }}
        >
          <ArticleIcon sx={{ paddingRight: '5px' }} />
          Get logs
        </CustomButton>
        <CustomButton
          onClick={clearLogs}
          variant="contained"
          color="warning"
          type="button"
          disabled={loadingLogs}
          sx={{
            fontFamily: 'inherit',
            backgroundColor: 'primary.main',
            color: 'primary.contrastText',
            '&:hover': {
              backgroundColor: 'primary.dark'
            }
          }}
        >
          <ClearIcon sx={{ paddingRight: '5px' }} />
          Clear logs
        </CustomButton>
      </div>
    </div>
  );
};

export default LogBox;

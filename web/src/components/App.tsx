/* Web app main component */
import React, { useState, useEffect, useCallback, useRef } from 'react';
import { AppSettings } from './commonInterfaces';
import { ThemeProvider, CssBaseline } from '@mui/material';
import { useParameters } from './context/ParametersContext';
import { CustomStyledIconButton, CustomButton } from './CustomComponents';
import { lightTheme, darkTheme } from '../theme';
import { useLocalStorage, useScreenSizes } from '../helpers/hooks.jsx';
import { drawerWidth, drawerHeight, appbarHeight } from './constants';
import { enableLogging } from '../helpers/logger';
import { useGlobalContext } from './context/GlobalContext.js';
import { serverPost } from '../helpers/cgihelper';
import { getBackendWebSocketUrl } from './getBackendWebSocketUrl';
import AboutModal from './AboutModal';
import AlertSnackbar from './AlertSnackbar';
import VideoPlayer from './VideoPlayer';

/* Game related stuff */
import QuakeInputHandler from './QuakeInputHandler';
import LogBox from './LogBox';
import InfoBox from './InfoBox';

import BugReportOutlinedIcon from '@mui/icons-material/BugReportOutlined';
import CheckCircleOutlinedIcon from '@mui/icons-material/CheckCircleOutlined';
import ErrorOutlinedIcon from '@mui/icons-material/ErrorOutlined';
import PlayArrowIcon from '@mui/icons-material/PlayArrow';
import SaveIcon from '@mui/icons-material/Save';
import StopIcon from '@mui/icons-material/Stop';

/* MUI */
import { styled } from '@mui/material/styles';
import MuiAppBar, { AppBarProps as MuiAppBarProps } from '@mui/material/AppBar';
import Box from '@mui/material/Box';
import Chip from '@mui/material/Chip';
import ChevronLeftIcon from '@mui/icons-material/ChevronLeft';
import ChevronRightIcon from '@mui/icons-material/ChevronRight';
import ContrastIcon from '@mui/icons-material/Contrast';
import Divider from '@mui/material/Divider';
import Drawer from '@mui/material/Drawer';
import Fab from '@mui/material/Fab';
import Fade from '@mui/material/Fade';
import InfoOutlinedIcon from '@mui/icons-material/InfoOutlined';
import KeyboardArrowDownIcon from '@mui/icons-material/KeyboardArrowDown';
import KeyboardArrowUpIcon from '@mui/icons-material/KeyboardArrowUp';
import MenuIcon from '@mui/icons-material/Menu';
import Toolbar from '@mui/material/Toolbar';
import Tooltip from '@mui/material/Tooltip';
import Typography from '@mui/material/Typography';

/******************************************************************************/

{
  /* Main content */
}
const Main = styled('main', {
  shouldForwardProp: (prop) => prop !== 'open' && prop !== 'isMobile'
})<{
  open?: boolean;
  isMobile?: boolean;
}>(({ theme, open, isMobile }) => ({
  flexGrow: 1,
  display: 'flex',
  flexDirection: 'column',
  minWidth: 0,
  minHeight: 0,
  padding: theme.spacing(isMobile ? 0 : '4px'),
  transition: theme.transitions.create('margin', {
    easing: theme.transitions.easing.sharp,
    duration: theme.transitions.duration.leavingScreen
  }),
  ...(isMobile
    ? { marginBottom: open ? drawerHeight : appbarHeight }
    : { marginRight: open ? drawerWidth : 0 }),
  ...(open && {
    transition: theme.transitions.create('margin', {
      easing: theme.transitions.easing.easeOut,
      duration: theme.transitions.duration.enteringScreen
    }),
    position: 'relative'
  })
}));

interface AppBarProps extends MuiAppBarProps {
  open?: boolean;
  isMobile?: boolean;
}

{
  /* Application header bar */
}
const AppBar = styled(MuiAppBar, {
  shouldForwardProp: (prop) => prop !== 'open' && prop !== 'isMobile'
})<AppBarProps>(({ theme, open, isMobile }) => ({
  overflowX: 'auto',
  WebkitOverflowScrolling: 'touch',
  scrollbarWidth: 'none',
  backgroundColor: theme.palette.background.paper,
  color: theme.palette.text.primary,
  backgroundImage: 'none',
  whiteSpace: 'nowrap',
  transition: theme.transitions.create(
    isMobile ? 'margin' : ['margin', 'width'],
    {
      easing: theme.transitions.easing.sharp,
      duration: theme.transitions.duration.leavingScreen
    }
  ),
  ...(open &&
    !isMobile && {
      width: `calc(100% - ${drawerWidth}px)`,
      marginRight: `${drawerWidth}px`,
      transition: theme.transitions.create(['margin', 'width'], {
        easing: theme.transitions.easing.easeOut,
        duration: theme.transitions.duration.enteringScreen
      })
    }),
  /* Horizontal scrollbar style */
  '&::-webkit-scrollbar': {
    height: '8px',
    backgroundColor: 'transparent'
  },
  '&::-webkit-scrollbar-thumb': {
    backgroundColor:
      theme.palette.mode === 'dark'
        ? theme.palette.grey[600]
        : theme.palette.grey[400],
    borderRadius: '6px'
  },
  '&::-webkit-scrollbar-track': {
    backgroundColor:
      theme.palette.mode === 'dark'
        ? theme.palette.grey[800]
        : theme.palette.grey[200]
  },
  '& .MuiToolbar-root': {
    minHeight: appbarHeight
  }
}));

const DrawerHeader = styled('div')(({ theme }) => ({
  display: 'flex',
  alignItems: 'center',
  padding: theme.spacing(0, 1),
  height: appbarHeight,
  justifyContent: 'flex-end',
  flexShrink: 0
}));

/******************************************************************************/

const App: React.FC = () => {
  /* Local state */
  const [aboutModalOpen, setAboutModalOpen] = useState<boolean>(false);

  /* Local storage state */
  const [drawerOpen, setDrawerOpen] = useLocalStorage('drawerOpen', true);

  /* Global context */
  const {
    openAlert,
    setOpenAlert,
    alertContent,
    alertSeverity,
    currentTheme,
    setCurrentTheme,
    appSettings,
    setAppSettings,
    handleOpenAlert
  } = useGlobalContext();

  /* Refs */
  const drawerScrollRef = useRef<HTMLDivElement>(null);
  const gameInputRef = useRef<HTMLDivElement>(null);

  /* Global parameter list */
  const { parameters } = useParameters();
  const ProdFullName = parameters?.['root.Brand.ProdFullName'];
  const ProdShortName = parameters?.['root.Brand.ProdShortName'];
  const ProdVariant = parameters?.['root.Brand.ProdVariant'];

  /* Theme */
  const theme = currentTheme === 'dark' ? darkTheme : lightTheme;

  /* Screen size */
  const { isMobile } = useScreenSizes();

  enableLogging(true);

  const handleDrawerClose = useCallback(() => {
    setDrawerOpen(false);
  }, [setDrawerOpen]);

  const toggleDrawerOpen = useCallback(() => {
    setDrawerOpen(!drawerOpen);
    /* Scroll drawer to top if closed in mobile mode */
    if (drawerOpen && drawerScrollRef.current) {
      drawerScrollRef.current.scrollTo({ top: 0 });
    }
  }, [drawerOpen, setDrawerOpen]);

  const toggleTheme = useCallback(() => {
    const newTheme = currentTheme === 'light' ? 'dark' : 'light';
    setCurrentTheme(newTheme);
    handleOpenAlert(`Use theme: ${newTheme}`, 'success');
  }, [currentTheme, setCurrentTheme, handleOpenAlert]);

  /* Modal open/close handlers */
  const handleOpenAboutModal = () => {
    setAboutModalOpen(true);
  };
  const handleCloseAboutModal = () => setAboutModalOpen(false);

  /* Toggle debug features of the site */
  const toggleDebug = () => {
    setAppSettings((prevSettings: AppSettings) => {
      const newDebugState = !prevSettings.debug;
      return {
        ...prevSettings,
        debug: newDebugState
      };
    });
    handleOpenAlert(`Debug mode: ${!appSettings.debug}`, 'success');
  };

  /* Alert handler */
  const handleCloseAlert = (
    event?: React.SyntheticEvent | Event,
    reason?: string
  ) => {
    if (reason === 'clickaway') {
      return;
    }
    setOpenAlert(false);
  };

  /****************************************************************************/
  /* GAME */
  const CONTROL_CGI = '/axis-cgi/applications/control.cgi';
  const TIMEOUT = 2000;
  const INPUT_CAPTURE_MESSAGE_TIMEOUT = 4000;

  /* FIXME: Game state */
  const [isLoading, setLoading] = useState<boolean>(false);
  const [isRunning, setRunning] = useState<boolean>(false);
  const [loadingMessage, setLoadingMessage] = useState<string>('');
  const [errorResp, setErrorResp] = useState<string>('');
  const [connectionError, setConnectionError] = useState<string>('');
  const [controlsConnected, setControlsConnected] = useState<boolean>(false);
  const [pointerCaptured, setPointerCaptured] = useState<boolean>(false);
  const [showCapturedMessage, setShowCapturedMessage] = useState<boolean>(true);

  const socketRef = useRef<WebSocket | null>(null);
  const reconnectTimerRef = useRef<number | null>(null);

  /* Websocket endpoint */
  const wsAddress = getBackendWebSocketUrl();

  useEffect(() => {
    if (!pointerCaptured) {
      setShowCapturedMessage(true);
      return;
    }

    const timer = window.setTimeout(() => {
      setShowCapturedMessage(false);
    }, INPUT_CAPTURE_MESSAGE_TIMEOUT);

    return () => window.clearTimeout(timer);
  }, [pointerCaptured]);

  /* Websocket setup */
  useEffect(() => {
    let shouldReconnect = true;

    const connectWebSocket = () => {
      const socket = new WebSocket(wsAddress);
      socketRef.current = socket;

      /* WS onopen */
      socket.onopen = () => {
        setConnectionError('');
        setControlsConnected(true);
        setRunning(true);
      };
      /* WS onclose */
      socket.onclose = () => {
        setControlsConnected(false);
        setRunning(false);
        if (!shouldReconnect) {
          return;
        }
        setConnectionError('WebSocket connection closed. Reconnecting...');
        reconnectTimerRef.current = window.setTimeout(
          connectWebSocket,
          TIMEOUT
        );
      };
      /* WS onerror */
      socket.onerror = (error: Event) => {
        console.error(
          'Error: Could not establish WebSocket connection:',
          error
        );
        setConnectionError('Error: Could not establish WebSocket connection.');
        setControlsConnected(false);
        setRunning(false);
      };
    };
    connectWebSocket();

    return () => {
      shouldReconnect = false;
      if (reconnectTimerRef.current !== null) {
        window.clearTimeout(reconnectTimerRef.current);
        reconnectTimerRef.current = null;
      }
      if (socketRef.current) {
        socketRef.current.close();
        socketRef.current = null;
      }
    };
  }, [wsAddress]);

  /* WebSocket status indicator */
  const WsStatus = () => {
    return (
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          justifyContent: 'center',
          alignItems: 'center',
          gap: 1,
          my: 1
        }}
      >
        <Chip
          icon={
            controlsConnected ? (
              <CheckCircleOutlinedIcon />
            ) : (
              <ErrorOutlinedIcon />
            )
          }
          label={
            controlsConnected ? 'Controls connected' : 'Controls disconnected'
          }
          color={controlsConnected ? 'success' : 'error'}
          variant="outlined"
          sx={{
            fontWeight: 600,
            '& .MuiChip-icon': {
              color: 'inherit'
            }
          }}
        />
        {!controlsConnected &&
          appSettings.debug === true &&
          connectionError && (
            <Typography
              variant="caption"
              color="text.secondary"
              sx={{
                maxWidth: '100%',
                textAlign: 'center',
                overflowWrap: 'anywhere'
              }}
            >
              {connectionError}
            </Typography>
          )}
      </Box>
    );
  };

  /* Start or stop the app */
  const startApp = (key: 'start' | 'stop'): void => {
    const setData = async () => {
      setLoading(true);
      setLoadingMessage(
        key === 'start' ? 'Starting Quake II ...' : 'Stopping Quake II ...'
      );
      const query = new URLSearchParams({
        action: key,
        package: 'acap_quake2'
      });

      setErrorResp('');
      try {
        const resp = await serverPost(`${CONTROL_CGI}?${query.toString()}`);
        const responseText = (await resp.text()).trim();
        if (!resp.ok || responseText !== 'OK') {
          const errorMessage =
            responseText ||
            resp.statusText ||
            'Application control request failed';
          console.error(errorMessage);
          setErrorResp(errorMessage);
          return;
        }
        if (key === 'start') {
          setRunning(true);
        } else {
          setRunning(false);
          setControlsConnected(false);
        }
      } catch (error) {
        const errorMessage =
          error instanceof Error ? error.message : String(error);
        setErrorResp(errorMessage);
        console.error('Request failed:', error);
      } finally {
        setLoading(false);
      }
    };

    setData();
  };

  const StartStop = () => {
    return (
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          width: '100%',
          marginTop: '20px',
          marginBottom: '20px'
        }}
      >
        <CustomButton
          onClick={() => startApp('start')}
          variant="contained"
          type="button"
          disabled={isLoading || isRunning}
        >
          <PlayArrowIcon sx={{ paddingRight: '5px' }} />
          START
        </CustomButton>
        <div style={{ marginLeft: '20px' }} />
        <CustomButton
          onClick={() => startApp('stop')}
          variant="contained"
          color="warning"
          type="button"
          disabled={isLoading || !isRunning}
        >
          <StopIcon sx={{ paddingRight: '5px' }} />
          STOP
        </CustomButton>
      </div>
    );
  };

  /****************************************************************************/

  const contentMain = () => {
    return (
      <>
        {/* Application header bar */}
        <AppBar position="fixed" open={drawerOpen} isMobile={isMobile}>
          <Toolbar
            sx={{
              display: 'flex',
              justifyContent: 'space-between'
            }}
          >
            {/* Left-side action buttons */}
            <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.5 }}>
              {/* Info Button */}
              <Tooltip title="About info" arrow>
                <div>
                  <CustomStyledIconButton
                    color="inherit"
                    aria-label="about info"
                    onClick={handleOpenAboutModal}
                    edge="end"
                    sx={{ p: 0.5 }}
                  >
                    <InfoOutlinedIcon
                      sx={{
                        width: '20px',
                        height: '20px',
                        color: 'text.secondary'
                      }}
                    />
                  </CustomStyledIconButton>
                </div>
              </Tooltip>

              {/* Theme Toggle Button */}
              <Tooltip title="Toggle theme" arrow>
                <div>
                  <CustomStyledIconButton
                    color="inherit"
                    aria-label="toggle theme"
                    onClick={toggleTheme}
                    edge="end"
                    sx={{ p: 0.5 }}
                  >
                    <ContrastIcon
                      sx={{
                        width: '20px',
                        height: '20px',
                        color: 'text.secondary'
                      }}
                    />
                  </CustomStyledIconButton>
                </div>
              </Tooltip>

              {/* Debug Toggle Button */}
              <Tooltip title="Toggle debug" arrow>
                <div>
                  <CustomStyledIconButton
                    color="inherit"
                    aria-label="toggle debug"
                    onClick={toggleDebug}
                    edge="end"
                    sx={{ p: 0.5, position: 'relative' }}
                  >
                    <BugReportOutlinedIcon
                      sx={{
                        width: '20px',
                        height: '20px',
                        color: 'text.secondary'
                      }}
                    />
                    {/* Cross line overlay */}
                    {!appSettings.debug && (
                      <Box
                        sx={{
                          position: 'absolute',
                          top: '50%',
                          left: '50%',
                          width: '24px',
                          height: '2px',
                          backgroundColor: 'error.main',
                          transform: 'translate(-50%, -50%) rotate(45deg)',
                          zIndex: 1
                        }}
                      />
                    )}
                  </CustomStyledIconButton>
                </div>
              </Tooltip>
            </Box>

            {/* Title and Logo */}
            <Box
              sx={{
                flexGrow: 1,
                display: 'flex',
                justifyContent: 'center',
                alignItems: 'center'
              }}
            >
              {/* Title */}
              <Fade in={true} timeout={1000} mountOnEnter unmountOnExit>
                <Typography
                  variant={isMobile ? 'h6' : 'h5'}
                  sx={{ color: 'text.primary' }}
                  noWrap
                  component="div"
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    fontFamily: 'inherit'
                  }}
                >
                  {/* Debug mode */}
                  {appSettings.debug && (
                    <Tooltip
                      title="Debug mode is now active. Additional prints and debug features enabled."
                      arrow
                    >
                      <Chip
                        size="small"
                        color="error"
                        icon={<BugReportOutlinedIcon />}
                        label={'Debug mode'}
                        sx={{ marginRight: '8px' }}
                      />
                    </Tooltip>
                  )}
                  {/* Website Name and Product Full Name */}
                  {import.meta.env.VITE_WEBSITE_NAME} @{' '}
                  {isMobile ? ProdShortName : ProdFullName}
                  {/* Product variant */}
                  {!isMobile && ProdVariant && (
                    <Tooltip title="Product variant" arrow>
                      <Chip label={ProdVariant} size="small" sx={{ ml: 1 }} />
                    </Tooltip>
                  )}
                </Typography>
              </Fade>
            </Box>

            {/* Menu button (right-aligned) */}
            <Tooltip
              title={drawerOpen ? 'Close the menu' : 'Open the menu'}
              arrow
              placement="left"
            >
              <div>
                <CustomStyledIconButton
                  color="inherit"
                  aria-label="open drawer"
                  onClick={toggleDrawerOpen}
                  edge="end"
                  sx={{
                    ...(!isMobile && drawerOpen ? { display: 'none' } : {})
                  }}
                >
                  <MenuIcon
                    sx={{
                      width: '20px',
                      height: '20px',
                      color: 'text.secondary'
                    }}
                  />
                </CustomStyledIconButton>
              </div>
            </Tooltip>
          </Toolbar>
        </AppBar>

        {/* Drawer menu */}
        <Drawer
          sx={{
            flexShrink: 0,
            '& .MuiDrawer-paper': {
              border: 'none',
              boxShadow: theme.shadows[4],
              boxSizing: 'border-box',
              display: 'flex',
              flexDirection: 'column',
              overflow: 'hidden',
              position: 'fixed',
              ...(isMobile
                ? {
                    height: drawerOpen ? drawerHeight : appbarHeight,
                    bottom: 0,
                    width: '100%'
                  }
                : {
                    width: drawerWidth,
                    right: 0,
                    top: 0,
                    height: '100%'
                  })
            }
          }}
          variant="persistent"
          anchor={isMobile ? 'bottom' : 'right'}
          /* Menu always open to show drawer header in mobile mode */
          open={isMobile ? true : drawerOpen}
        >
          <DrawerHeader
            sx={{
              display: 'flex',
              justifyContent: 'center',
              alignItems: 'center',
              position: 'relative',
              width: '100%'
            }}
          >
            <Box
              sx={{
                display: 'flex',
                justifyContent: 'center',
                alignItems: 'center',
                flexGrow: 1
              }}
            >
              <Typography
                variant="h6"
                color="text.primary"
                sx={{ fontWeight: 700, letterSpacing: '0.08em' }}
              >
                QUAKE II
              </Typography>
            </Box>
            {/* Menu toggle button */}
            <Tooltip
              title={drawerOpen ? 'Close the menu' : 'Open the menu'}
              arrow
              placement="bottom"
            >
              <div
                style={{
                  position: 'absolute',
                  left: '8px',
                  top: '50%',
                  transform: 'translateY(-50%)'
                }}
              >
                <CustomStyledIconButton
                  onClick={isMobile ? toggleDrawerOpen : handleDrawerClose}
                >
                  {isMobile ? (
                    drawerOpen ? (
                      <KeyboardArrowDownIcon />
                    ) : (
                      <KeyboardArrowUpIcon />
                    )
                  ) : theme.direction === 'ltr' ? (
                    <ChevronRightIcon />
                  ) : (
                    <ChevronLeftIcon />
                  )}
                </CustomStyledIconButton>
              </div>
            </Tooltip>
          </DrawerHeader>

          {/* Drawer content starts here */}
          <Divider />

          {/* Scrollable drawer content */}
          <Box
            ref={drawerScrollRef}
            sx={{
              flex: 1,
              minHeight: 0,
              overflowY: 'auto',
              overflowX: 'hidden',
              '&::-webkit-scrollbar': {
                width: '8px',
                backgroundColor: 'transparent'
              },
              '&::-webkit-scrollbar-thumb': {
                backgroundColor: (theme) =>
                  theme.palette.mode === 'dark'
                    ? theme.palette.grey[600]
                    : theme.palette.grey[400],
                borderRadius: '6px'
              },
              '&::-webkit-scrollbar-track': {
                backgroundColor: (theme) =>
                  theme.palette.mode === 'dark'
                    ? theme.palette.grey[800]
                    : theme.palette.grey[200]
              }
            }}
          >
            {/* NOTE: Drawer content here */}
            <Box sx={{ paddingBottom: 1, pl: 1, pr: 1 }}>
              <StartStop />
              {appSettings.debug ? <LogBox /> : <InfoBox />}
              <WsStatus />
              {!isRunning && !isLoading && (
                <Fade in={true} timeout={1000} mountOnEnter unmountOnExit>
                  <h3
                    style={{
                      display: 'flex',
                      justifyContent: 'center',
                      paddingTop: '1em',
                      color: theme.palette.text.primary,
                      fontFamily: 'inherit'
                    }}
                  >
                    Press START to play!
                  </h3>
                </Fade>
              )}
              <h3 style={{ color: 'white' }}>
                {!isRunning && isLoading ? (
                  <Fade in={true} timeout={1000} mountOnEnter unmountOnExit>
                    <div style={{ display: 'flex', justifyContent: 'center' }}>
                      <SaveIcon
                        className="spinner"
                        style={{
                          color: theme.palette.primary.main,
                          width: '24px',
                          height: '24px',
                          marginTop: '20px'
                        }}
                      />
                      <div style={{ marginLeft: '10px' }} />
                      <div
                        style={{
                          marginTop: '22px',
                          fontFamily: 'inherit',
                          color: theme.palette.text.primary
                        }}
                      >
                        {loadingMessage}
                      </div>
                    </div>
                  </Fade>
                ) : (
                  ''
                )}
              </h3>
              <h3 style={{ color: 'white' }}>{errorResp}</h3>
            </Box>
          </Box>
        </Drawer>

        {/* Main content */}
        <Main open={drawerOpen} isMobile={isMobile}>
          <DrawerHeader />
          {/* Video Player and game input capture surface */}
          <Box
            ref={gameInputRef}
            tabIndex={0}
            sx={{
              position: 'relative',
              display: 'flex',
              flexGrow: 1,
              minHeight: 0,
              outline: 'none'
            }}
          >
            <VideoPlayer />
            <Fade
              in={!pointerCaptured || showCapturedMessage}
              timeout={500}
              unmountOnExit
            >
              <Box
                sx={{
                  position: 'absolute',
                  top: pointerCaptured ? 8 : '50%',
                  left: '50%',
                  transform: pointerCaptured
                    ? 'translateX(-50%)'
                    : 'translate(-50%, -50%)',
                  zIndex: 20,
                  maxWidth: 'calc(100% - 16px)',
                  px: 2,
                  py: 1,
                  borderRadius: 1,
                  bgcolor: 'rgba(0, 0, 0, 0.7)',
                  color: 'white',
                  fontSize: '14px',
                  textAlign: 'center',
                  pointerEvents: 'none'
                }}
              >
                {pointerCaptured
                  ? 'Input captured. Press Esc once to release the mouse, then press Esc again to open the Quake II menu.'
                  : 'Click anywhere in the game to capture mouse and keyboard input.'}
              </Box>
            </Fade>
          </Box>
        </Main>

        {/* Alert Snackbar */}
        <AlertSnackbar
          openAlert={openAlert}
          alertSeverity={alertSeverity}
          alertContent={alertContent}
          handleCloseAlert={handleCloseAlert}
          alertOffset={`calc(${appbarHeight} + ${theme.spacing(2)})`}
        />

        {/* About Modal */}
        <AboutModal open={aboutModalOpen} handleClose={handleCloseAboutModal} />

        {/* Scroll-to-Top Button for mobile */}
        {isMobile && drawerOpen && (
          <Fab
            disableRipple
            color="primary"
            size="small"
            onClick={() => {
              if (drawerScrollRef.current) {
                drawerScrollRef.current.scrollTo({
                  top: 0,
                  behavior: 'smooth'
                });
              }
            }}
            sx={{
              position: 'fixed',
              bottom: '16px',
              right: '16px',
              zIndex: 2000
            }}
          >
            <KeyboardArrowUpIcon />
          </Fab>
        )}
        <QuakeInputHandler
          socketRef={socketRef}
          targetRef={gameInputRef}
          onCaptureChange={setPointerCaptured}
        />
      </>
    );
  };

  return (
    <ThemeProvider theme={theme}>
      <Box sx={{ display: 'flex', flexDirection: 'column', height: '100vh' }}>
        <CssBaseline />
        {contentMain()}
      </Box>
    </ThemeProvider>
  );
};

export default App;
